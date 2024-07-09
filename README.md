# MultiThreadFileTransfer

MultithreadFileTransfer is a C application that allows for efficient file transfer between two folders using multithreading. The application provides a graphical user interface (GUI) built with GTK, enabling users to easily select and transfer files between two predefined directories.


## Features

### Multithreaded File Transfer: 

Utilizes multithreading to transfer files concurrently, improving efficiency and performance.


### GTK-Based GUI: 

User-friendly graphical interface for selecting and transferring files.


### Bidirectional Transfer: 

Supports transferring files from Folder 1 to Folder 2 and vice versa.


### File Synchronization: 

Ensures data integrity and proper synchronization during transfer.


### Thread Synchronization: 

Implements ticket locks for thread-safe operations.


## Prerequisites and How to Run the Project


Install GTK on your Linux System

`$ sudo apt-get install libgtk-3-dev`


Make 2 folders 'folder1' and 'folder2' in the same directory


Compile the source code:

`$ gcc -o executableGUI FileTransferGUI.c `pkg-config --cflags --libs gtk+-3.0` -lpthread`


Run the application:

`$ ./executableGUI`


## Technologies and Libraries Used
C: Core Application logic and programming


GTK 3: GUI toolkit used to create the graphical user interface.


POSIX Threads (pthread): Used for multithreading support in file transfer operations.


## How It Works
### Multithreading:

The application creates separate threads for sending and receiving files.
A named pipe is used for inter-thread communication during file transfer.
Ticket locks ensure thread-safe operations.


### GTK Interface:

The GUI allows users to select files and initiate transfer operations.
The file lists in the GUI are populated by reading the contents of the predefined directories.

