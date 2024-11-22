#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_FILES 10
#define MAX_FILENAME_LENGTH 256
#define BUFFER_SIZE 8192
#define MAX_PATH_LENGTH 1024

char *SOURCE_DIR;
char *DEST_DIR;
struct FileInfo
{
    int index;
    char filename[MAX_FILENAME_LENGTH];
    int isSelected;
    GtkWidget *checkbox;
};

struct FileInfo fileInfos1[MAX_FILES];
struct FileInfo fileInfos2[MAX_FILES];
struct FileInfo selectedFiles[MAX_FILES];
int numSelectedFiles = 0;

GtkWidget *main_grid; // Main grid for the main window
GtkWidget *window;    // Main window

// Function declarations
void on_back_button_clicked(GtkWidget *button, gpointer data);
gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
void on_checkbox_selected(GtkWidget *checkbox, gpointer data);
void *transfer_thread(void *arg);
void readDirectory(const char *dirname, struct FileInfo *fileInfos);
void updateDirectoryViews();
void on_button_clicked(GtkWidget *button, gpointer data);
void on_folder_button_clicked(GtkWidget *button, gpointer data);

// Function to handle the "delete-event" signal for the window
gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    gtk_main_quit(); // Quit the GTK main loop when the window is closed
    return TRUE;     // Prevent the default destroy signal handler from executing
}

// Callback function to handle checkbox state change
void on_checkbox_selected(GtkWidget *checkbox, gpointer data)
{
    struct FileInfo *fileInfo = (struct FileInfo *)g_object_get_data(G_OBJECT(checkbox), "file_info");
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox)))
        fileInfo->isSelected = 1; // Select
    else
        fileInfo->isSelected = 0; // Deselect
}

void *transfer_thread(void *arg)
{
    struct FileInfo *file_info = (struct FileInfo *)arg;
    char *filename = file_info->filename;
    pid_t pid;
    char fifo_name[MAX_PATH_LENGTH];
    snprintf(fifo_name, sizeof(fifo_name), "fifo_%s", filename);
    if (mkfifo(fifo_name, 0666) == -1)
    {
        perror("Error creating FIFO pipe");
        pthread_exit(NULL);
    }

    pid = fork();

    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        // Sender process
        FILE *file_ptr;
        char full_file_path[MAX_PATH_LENGTH];
        snprintf(full_file_path, sizeof(full_file_path), "%s/%s", SOURCE_DIR, filename);
        file_ptr = fopen(full_file_path, "rb");
        if (file_ptr == NULL)
        {
            perror("Error opening file");
            pthread_exit(NULL);
        }
        unsigned char buffer[BUFFER_SIZE];
        int res = open(fifo_name, O_WRONLY);
        if (res == -1)
        {
            perror("Error opening named pipe for writing");
            fclose(file_ptr);
            pthread_exit(NULL);
        }
        while (1)
        {
            size_t bytes_read = fread(buffer, 1, BUFFER_SIZE, file_ptr);
            if (bytes_read == 0)
            {
                if (feof(file_ptr))
                {
                    // EOF reached
                    close(res);
                    fclose(file_ptr);
                }
                else
                {
                    perror("Error reading file");
                    pthread_exit(NULL);
                }
                break;
            }

            ssize_t bytes_written = write(res, buffer, bytes_read);
            if (bytes_written != bytes_read)
            {
                perror("Error writing to named pipe");
                break; // Exit the loop if write operation fails
            }
        }
    }
    else
    {
        // Receiver process
        char full_file_path_dest[MAX_PATH_LENGTH];
        snprintf(full_file_path_dest, sizeof(full_file_path_dest), "%s/%s", DEST_DIR, filename);

        // Check if file already exists and rename if necessary
        int file_number = 0;
        char final_file_path[MAX_PATH_LENGTH];
        char *dot = strrchr(filename, '.');
        if (dot)
        {
            // Separate the base name and extension
            size_t basename_length = dot - filename;
            char basename[MAX_FILENAME_LENGTH];
            strncpy(basename, filename, basename_length);
            basename[basename_length] = '\0';

            char extension[MAX_FILENAME_LENGTH];
            strcpy(extension, dot);

            // Construct the initial full path
            snprintf(final_file_path, sizeof(final_file_path), "%s/%s%s", DEST_DIR, basename, extension);

            // Increment the file number if the file already exists
            while (access(final_file_path, F_OK) == 0)
            {
                file_number++;
                snprintf(final_file_path, sizeof(final_file_path), "%s/%s(%d)%s", DEST_DIR, basename, file_number, extension);
            }
        }
        else
        {
            // No extension found, just use the base name
            strcpy(final_file_path, full_file_path_dest);
            while (access(final_file_path, F_OK) == 0)
            {
                file_number++;
                snprintf(final_file_path, sizeof(final_file_path), "%s/%s(%d)", DEST_DIR, filename, file_number);
            }
        }

        FILE *dest_file = fopen(final_file_path, "wb");
        unsigned char buffer[BUFFER_SIZE];
        int res = open(fifo_name, O_RDONLY);
        if (dest_file == NULL)
        {
            perror("Error creating/opening received file");
            close(res);
            pthread_exit(NULL);
        }

        if (res == -1)
        {
            perror("Error opening named pipe for reading");
            fclose(dest_file);
            pthread_exit(NULL);
        }

        while (1)
        {
            size_t bytes_read = read(res, buffer, BUFFER_SIZE);
            if (bytes_read <= 0)
            {
                break;
            }
            ssize_t bytes_written = fwrite(buffer, 1, bytes_read, dest_file);

            if (bytes_written != bytes_read)
            {
                perror("Error writing to file");
                break;
            }
        }

        // Close file pointer and named pipe
        close(res);
        fclose(dest_file);
        if (unlink(fifo_name) == -1)
        {
            perror("Error removing FIFO pipe");
        }

        // Log receiver process completion
        printf("Receiver process for file %s completed.\n", filename);

        // Untick the checkbox on successful file transfer
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(file_info->checkbox), FALSE);
    }
    pthread_exit(NULL);
}

// Function to read the contents of a directory and populate fileInfos array
void readDirectory(const char *dirname, struct FileInfo *fileInfos)
{
    DIR *dir = opendir(dirname);
    if (dir != NULL)
    {
        struct dirent *ent;
        int counter = 0;
        while ((ent = readdir(dir)) != NULL && counter < MAX_FILES)
        {
            if (ent->d_name[0] != '.')
            {
                fileInfos[counter].index = counter;
                strcpy(fileInfos[counter].filename, ent->d_name);
                fileInfos[counter].isSelected = 0;
                fileInfos[counter].checkbox = NULL;
                counter++;
            }
        }
        closedir(dir);
    }
    else
    {
        perror("Error opening directory");
    }
}

// Function to update the directory views
void updateDirectoryViews()
{
    readDirectory("./Folder1", fileInfos1);
    readDirectory("./Folder2", fileInfos2);
}

// Callback function to handle button click for transferring files
void on_button_clicked(GtkWidget *button, gpointer data)
{
    // Reset numSelectedFiles
    numSelectedFiles = 0;

    if (strcmp(gtk_widget_get_name(button), "Folder1") == 0)
    {
        SOURCE_DIR = "./Folder1";
        DEST_DIR = "./Folder2";
    }
    else
    {
        SOURCE_DIR = "./Folder2";
        DEST_DIR = "./Folder1";
    }

    // Iterate through fileInfos arrays and copy selected files to selectedFiles array
    struct FileInfo *fileInfos = (strcmp(gtk_widget_get_name(button), "Folder1") == 0) ? fileInfos1 : fileInfos2;
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (fileInfos[i].isSelected == 1)
        {
            selectedFiles[numSelectedFiles++] = fileInfos[i];
        }
    }

    // Print selected files on the terminal
    printf("Selected Files:\n");
    for (int i = 0; i < numSelectedFiles; i++)
    {
        printf("%s\n", selectedFiles[i].filename);
    }

    // Start transfer threads for selected files
    pthread_t threads[MAX_FILES];
    int transfer_success = 1;
    for (int i = 0; i < numSelectedFiles; i++)
    {
        if (pthread_create(&threads[i], NULL, transfer_thread, (void *)&selectedFiles[i]) != 0)
        {
            transfer_success = 0;
        }
    }

    // Wait for all transfer threads to complete
    for (int i = 0; i < numSelectedFiles; i++)
    {
        if (pthread_join(threads[i], NULL) != 0)
        {
            transfer_success = 0;
        }
    }

    // Update the directory views
    updateDirectoryViews();

    // Show success or failure message
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               transfer_success ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK,
                                               transfer_success ? "File(s) transferred successfully" : "File transfer failed");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Callback function to handle button click to select folder
void on_folder_button_clicked(GtkWidget *button, gpointer data)
{
    const char *folder = gtk_widget_get_name(button);
    if (strcmp(folder, "Folder1") == 0)
    {
        SOURCE_DIR = "./Folder1";
        DEST_DIR = "./Folder2";
    }
    else
    {
        SOURCE_DIR = "./Folder2";
        DEST_DIR = "./Folder1";
    }

    struct FileInfo *fileInfos = (strcmp(folder, "Folder1") == 0) ? fileInfos1 : fileInfos2;
    readDirectory(SOURCE_DIR, fileInfos);

    // Clear main grid
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(main_grid));
    for (iter = children; iter != NULL; iter = g_list_next(iter))
    {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    // Create a box for holding folder contents and checkboxes
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_grid_attach(GTK_GRID(main_grid), vbox, 0, 0, 2, 1);

    // Display folder contents
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (fileInfos[i].index != -1)
        {
            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

            GtkWidget *checkbox = gtk_check_button_new_with_label(fileInfos[i].filename);
            fileInfos[i].checkbox = checkbox;                                  // Store the checkbox widget
            g_object_set_data(G_OBJECT(checkbox), "file_info", &fileInfos[i]); // Attach file info to the checkbox
            g_signal_connect(checkbox, "toggled", G_CALLBACK(on_checkbox_selected), NULL);
            gtk_box_pack_start(GTK_BOX(hbox), checkbox, FALSE, FALSE, 0);
        }
    }

    // Create a button to add selected files
    GtkWidget *add_button = gtk_button_new_with_label("Add Selected Files");
    gtk_widget_set_name(add_button, folder);
    g_signal_connect(add_button, "clicked", G_CALLBACK(on_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), add_button, FALSE, FALSE, 0);

    // Create a back button to return to the main menu
    GtkWidget *back_button = gtk_button_new_with_label("Back");
    g_signal_connect(back_button, "clicked", G_CALLBACK(on_back_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), back_button, FALSE, FALSE, 0);

    gtk_widget_show_all(main_grid);
}

void on_description_button_clicked(GtkWidget *button, gpointer data) {
    // Create a new window for project description
    GtkWidget *description_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(description_window), "Project Description");
    gtk_window_set_default_size(GTK_WINDOW(description_window), 800, 600); // Set a larger default size for better readability

    // Create a scrolled window to contain the text view
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(description_window), scrolled_window);

    // Create a text view widget to display the project description
    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

    // Create a text buffer and set the text for the text view
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    const char *description_text = "In this project, the system is designed to facilitate efficient file transfer between a sender and a receiver. The sender’s role involves interfacing with a graphical user interface (GUI) to select and process multiple video files. These files are segmented and dispatched through individual threads over shared pipes, ensuring that each file’s end is properly signaled to the receiver. Once a file is completely transferred, the corresponding pipe is closed. On the other side, the receiver is tasked with opening and assembling the incoming segments into new video files. It must detect the end-of-file (EOF) marker to conclude each transfer, after which it terminates the associated thread and closes the file and pipe. Throughout this process, the GUI serves as a dashboard, displaying successful transfers or reporting any errors encountered. This dual-process system streamlines the transfer of video content, ensuring that multiple files can be handled concurrently and efficiently.";
    gtk_text_buffer_set_text(buffer, description_text, -1);

    // Set the background color and styling using CSS
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
                                    "textview { background-color: #333333; color: #ffffff; font-family: 'Arial'; font-size: 28px; }", // Dark gray background color, white text, increased font size
                                    -1,
                                    NULL);

    GtkStyleContext *context = gtk_widget_get_style_context(text_view);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Show all widgets in the description window
    gtk_widget_show_all(description_window);
}




void on_back_button_clicked(GtkWidget *button, gpointer data)
{
    // Clear main grid
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(main_grid));
    for (iter = children; iter != NULL; iter = g_list_next(iter))
    {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    // Recreate the main menu layout
    // Create a heading label
    // Create a heading label with custom CSS styling
	GtkWidget *heading_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(heading_label), "<span font_desc='30' foreground='#FFFFFF'>OS Project By Hira Sardar and Sarmad Sultan</span>");
	gtk_grid_attach(GTK_GRID(main_grid), heading_label, 0, 0, 2, 1);

    // Create buttons for Folder1 and Folder2
    GtkWidget *button1 = gtk_button_new_with_label("Folder1");
    gtk_widget_set_name(button1, "Folder1");
    g_signal_connect(button1, "clicked", G_CALLBACK(on_folder_button_clicked), NULL);
    gtk_grid_attach(GTK_GRID(main_grid), button1, 0, 1, 1, 1);

    GtkWidget *button2 = gtk_button_new_with_label("Folder2");
    gtk_widget_set_name(button2, "Folder2");
    g_signal_connect(button2, "clicked", G_CALLBACK(on_folder_button_clicked), NULL);
    gtk_grid_attach(GTK_GRID(main_grid), button2, 1, 1, 1, 1);
    
    GtkWidget *button3 = gtk_button_new_with_label("Description");
    gtk_widget_set_name(button2, "Description");
    g_signal_connect(button2, "clicked", G_CALLBACK(on_description_button_clicked), NULL);
    gtk_grid_attach(GTK_GRID(main_grid), button3, 2, 1, 1, 1);
    
    // Load CSS provider and apply styles to the window
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
                                    "window { background-color: #800020; }", // Vine red background color
                                    -1,
                                    NULL);

    // Apply the CSS provider to the window's style context
    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    gtk_widget_show_all(window);
}

int main(int argc, char *argv[])
{
    // Initialize GTK
gtk_init(&argc, &argv);

// Create the main window
window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
gtk_window_set_title(GTK_WINDOW(window), "File Transfer Program");
gtk_window_set_default_size(GTK_WINDOW(window), 600, 500);
g_signal_connect(window, "delete-event", G_CALLBACK(on_window_delete_event), NULL);

// Create a main grid
main_grid = gtk_grid_new();
gtk_grid_set_column_homogeneous(GTK_GRID(main_grid), TRUE);
gtk_grid_set_row_homogeneous(GTK_GRID(main_grid), TRUE);
gtk_container_add(GTK_CONTAINER(window), main_grid);

// Create a heading label
GtkWidget *heading_label = gtk_label_new(NULL);
gtk_label_set_markup(GTK_LABEL(heading_label), "<span font_desc='38' foreground='#FFFFFF'>End Semester Project By Hira Sardar and Sarmad Sultan!!</span>");
gtk_grid_attach(GTK_GRID(main_grid), heading_label, 0, 0, 2, 1);

// Create a choice label
GtkWidget *choice_label = gtk_label_new(NULL);
gtk_label_set_markup(GTK_LABEL(choice_label), "<span font_desc='30' foreground='#FFFFFF'>Choose the source folder to move files or click 'Description' to read details!!</span>");
gtk_grid_attach(GTK_GRID(main_grid), choice_label, 0, 1, 2, 1);

// Create buttons for Folder1 and Folder2
GtkWidget *button1 = gtk_button_new_with_label("Folder1");
gtk_widget_set_name(button1, "Folder1");
g_signal_connect(button1, "clicked", G_CALLBACK(on_folder_button_clicked), NULL);
gtk_grid_attach(GTK_GRID(main_grid), button1, 0, 2, 1, 1);

GtkWidget *button2 = gtk_button_new_with_label("Folder2");
gtk_widget_set_name(button2, "Folder2");
g_signal_connect(button2, "clicked", G_CALLBACK(on_folder_button_clicked), NULL);
gtk_grid_attach(GTK_GRID(main_grid), button2, 1, 2, 1, 1);

GtkWidget *button3 = gtk_button_new_with_label("Description");
gtk_widget_set_name(button3, "Description");
g_signal_connect(button3, "clicked", G_CALLBACK(on_description_button_clicked), NULL);
gtk_grid_attach(GTK_GRID(main_grid), button3, 2, 2, 1, 1);

// Load CSS provider and apply styles to the window
GtkCssProvider *provider = gtk_css_provider_new();
gtk_css_provider_load_from_data(provider,
                                "window { background-color: #800020; }", // Vine red background color
                                -1,
                                NULL);

// Apply the CSS provider to the window's style context
GtkStyleContext *context = gtk_widget_get_style_context(window);
gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

// Show all widgets in the main window
gtk_widget_show_all(window);

// Start the GTK main loop
gtk_main();

return 0;

}

