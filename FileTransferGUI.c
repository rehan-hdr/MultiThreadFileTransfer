#include <gtk/gtk.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>

#define FOLDER1_PATH "folder1"
#define FOLDER2_PATH "folder2"
#define BUFFER_SIZE 65536

typedef struct {
    char *_file;
    char *output_file;  // Added this member to the structure
    char pipe_name[256];
} thread_data_t;

typedef struct ticket_lock {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    unsigned long queue_head, queue_tail;
} ticket_lock_t;

ticket_lock_t send_lock = {PTHREAD_COND_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, 0, 0};
ticket_lock_t recv_lock = {PTHREAD_COND_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, 0, 0};

void ticket_lock(ticket_lock_t *ticket) {
    unsigned long queue_me;
    pthread_mutex_lock(&ticket->mutex);
    queue_me = ticket->queue_tail++;
    while (queue_me != ticket->queue_head) {
        pthread_cond_wait(&ticket->cond, &ticket->mutex);
    }
    pthread_mutex_unlock(&ticket->mutex);
}

void ticket_unlock(ticket_lock_t *ticket) {
    pthread_mutex_lock(&ticket->mutex);
    ticket->queue_head++;
    pthread_cond_broadcast(&ticket->cond);
    pthread_mutex_unlock(&ticket->mutex);
}

void *send_file(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    int _file_fd, pipe_fd, bytes_read;
    char buffer[BUFFER_SIZE];
    
    ticket_lock(&send_lock);

    printf("Sending File: %s\n", data->_file);

    pipe_fd = open(data->pipe_name, O_WRONLY);
    if (pipe_fd < 0) {
        perror("Failed to open pipe");
        exit(EXIT_FAILURE);
    }

    _file_fd = open(data->_file, O_RDONLY);
    if (_file_fd < 0) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    while ((bytes_read = read(_file_fd, buffer, BUFFER_SIZE)) > 0) {
        write(pipe_fd, buffer, bytes_read);
    }

    close(_file_fd);
    close(pipe_fd);

    ticket_unlock(&send_lock);

    pthread_exit(NULL);
}

void *receive_file(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    int output_fd, pipe_fd, bytes_read;
    char buffer[BUFFER_SIZE];
    
    ticket_lock(&recv_lock);

    printf("Receiving File: %s\n", data->output_file);

    pipe_fd = open(data->pipe_name, O_RDONLY);
    if (pipe_fd < 0) {
        perror("Failed to open pipe");
        exit(EXIT_FAILURE);
    }

    output_fd = open(data->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output_fd < 0) {
        perror("Failed to open output file");
        exit(EXIT_FAILURE);
    }

    while ((bytes_read = read(pipe_fd, buffer, BUFFER_SIZE)) > 0) {
        write(output_fd, buffer, bytes_read);
    }

    close(output_fd);
    close(pipe_fd);

    ticket_unlock(&recv_lock);

    pthread_exit(NULL);
}

void populate_list(GtkWidget *list, const char *path) {
    gtk_list_store_clear(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(list))));
    
    DIR *dir;
    struct dirent *entry;
    dir = opendir(path);
    
    if (dir == NULL) {
        perror("Unable to open directory");
        return;
    }
    
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(list)));
    GtkTreeIter iter;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, entry->d_name, -1);
        }
    }
    closedir(dir);
}

void refresh_folders(GtkWidget *button, gpointer data) {
    GtkWidget **lists = (GtkWidget **)data;
    populate_list(lists[0], FOLDER1_PATH);
    populate_list(lists[1], FOLDER2_PATH);
}

GtkWidget* create_file_list_view() {
    GtkWidget *list = gtk_tree_view_new();
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(list), GTK_TREE_MODEL(store));
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Files", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
    
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), GTK_SELECTION_MULTIPLE);
    
    return list;
}

void send_selected_files_to_folder2(GtkWidget *button, gpointer data) {
    GtkWidget *list = (GtkWidget *)data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
    GtkTreeModel *model;
    GList *selected_rows, *iter;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    selected_rows = gtk_tree_selection_get_selected_rows(selection, &model);

    if (selected_rows == NULL) {
        return;
    }

    const char *pipe_name = "file_transfer_pipe";
    mkfifo(pipe_name, 0666);

    pthread_t sender_thread;
    pthread_t receiver_thread;
    thread_data_t sender_data;
    thread_data_t receiver_data;

    for (iter = selected_rows; iter != NULL; iter = iter->next) {
        GtkTreePath *path = (GtkTreePath *)iter->data;
        GtkTreeIter tree_iter;
        gtk_tree_model_get_iter(model, &tree_iter, path);
        
        char *file_name;
        gtk_tree_model_get(model, &tree_iter, 0, &file_name, -1);

        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", FOLDER1_PATH, file_name);

        sender_data._file = file_path;
        snprintf(sender_data.pipe_name, sizeof(sender_data.pipe_name), "%s", pipe_name);

        char dest_file_path[512];
        snprintf(dest_file_path, sizeof(dest_file_path), "%s/%s", FOLDER2_PATH, file_name);

        receiver_data.output_file = dest_file_path;
        snprintf(receiver_data.pipe_name, sizeof(receiver_data.pipe_name), "%s", pipe_name);

        pthread_create(&sender_thread, NULL, send_file, (void *)&sender_data);
        pthread_create(&receiver_thread, NULL, receive_file, (void *)&receiver_data);

        pthread_join(sender_thread, NULL);
        pthread_join(receiver_thread, NULL);

        g_free(file_name);
    }

    g_list_free_full(selected_rows, (GDestroyNotify)gtk_tree_path_free);
}

void send_selected_files_to_folder1(GtkWidget *button, gpointer data) {
    GtkWidget *list = (GtkWidget *)data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
    GtkTreeModel *model;
    GList *selected_rows, *iter;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    selected_rows = gtk_tree_selection_get_selected_rows(selection, &model);

    if (selected_rows == NULL) {
        return;
    }

    const char *pipe_name = "file_transfer_pipe";
    mkfifo(pipe_name, 0666);

    pthread_t sender_thread;
    pthread_t receiver_thread;
    thread_data_t sender_data;
    thread_data_t receiver_data;

    for (iter = selected_rows; iter != NULL; iter = iter->next) {
        GtkTreePath *path = (GtkTreePath *)iter->data;
        GtkTreeIter tree_iter;
        gtk_tree_model_get_iter(model, &tree_iter, path);
        
        char *file_name;
        gtk_tree_model_get(model, &tree_iter, 0, &file_name, -1);

        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", FOLDER2_PATH, file_name);

        sender_data._file = file_path;
        snprintf(sender_data.pipe_name, sizeof(sender_data.pipe_name), "%s", pipe_name);

        char dest_file_path[512];
        snprintf(dest_file_path, sizeof(dest_file_path), "%s/%s", FOLDER1_PATH, file_name);

        receiver_data.output_file = dest_file_path;
        snprintf(receiver_data.pipe_name, sizeof(receiver_data.pipe_name), "%s", pipe_name);

        pthread_create(&sender_thread, NULL, send_file, (void *)&sender_data);
        pthread_create(&receiver_thread, NULL, receive_file, (void *)&receiver_data);

        pthread_join(sender_thread, NULL);
        pthread_join(receiver_thread, NULL);

        g_free(file_name);
    }

    g_list_free_full(selected_rows, (GDestroyNotify)gtk_tree_path_free);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Files you would want to copy");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 10);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    GtkWidget *folders_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(main_box), folders_box, TRUE, TRUE, 0);

    GtkWidget *folder1_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(folders_box), folder1_box, TRUE, TRUE, 0);

    GtkWidget *folder1_label = gtk_label_new("Folder 1");
    gtk_box_pack_start(GTK_BOX(folder1_box), folder1_label, FALSE, FALSE, 0);

    GtkWidget *folder1_list = create_file_list_view();
    gtk_box_pack_start(GTK_BOX(folder1_box), folder1_list, TRUE, TRUE, 0);

    GtkWidget *send_to_folder2_button = gtk_button_new_with_label("Send Selected to Folder 2");
    gtk_box_pack_start(GTK_BOX(folder1_box), send_to_folder2_button, FALSE, FALSE, 0);

    GtkWidget *folder2_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(folders_box), folder2_box, TRUE, TRUE, 0);

    GtkWidget *folder2_label = gtk_label_new("Folder 2");
    gtk_box_pack_start(GTK_BOX(folder2_box), folder2_label, FALSE, FALSE, 0);

    GtkWidget *folder2_list = create_file_list_view();
    gtk_box_pack_start(GTK_BOX(folder2_box), folder2_list, TRUE, TRUE, 0);

    GtkWidget *send_to_folder1_button = gtk_button_new_with_label("Send Selected to Folder 1");
    gtk_box_pack_start(GTK_BOX(folder2_box), send_to_folder1_button, FALSE, FALSE, 0);

    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh Folders");
    gtk_box_pack_start(GTK_BOX(main_box), refresh_button, FALSE, FALSE, 0);

    GtkWidget *lists[] = {folder1_list, folder2_list};

    g_signal_connect(refresh_button, "clicked", G_CALLBACK(refresh_folders), lists);
    g_signal_connect(send_to_folder2_button, "clicked", G_CALLBACK(send_selected_files_to_folder2), folder1_list);
    g_signal_connect(send_to_folder1_button, "clicked", G_CALLBACK(send_selected_files_to_folder1), folder2_list);

    populate_list(folder1_list, FOLDER1_PATH);
    populate_list(folder2_list, FOLDER2_PATH);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
