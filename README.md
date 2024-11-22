# **C-Code for Video Transmission**

This readme provides a detailed explanation of the file transfer program, its functionalities, usage instructions, and technical aspects.

---

## **Project Overview**
This program offers a user-friendly graphical user interface (GUI) for transferring files between two designated folders on your system. It caters specifically to video files but can be modified to handle other file types as well.

### **Key Features**
- **Multi-file Selection:** Select and transfer multiple video files simultaneously, enhancing efficiency.
- **Concurrent Transfer:** Utilizes threads to ensure smooth transfer of multiple files at once, optimizing transfer time.
- **End-of-File Handling:** Reliably handles end-of-file (EOF) markers to guarantee complete file transfer.
- **User-friendly Interface:** Provides a clear and intuitive GUI for selecting files, monitoring progress, and receiving transfer completion notifications.
- **Error Reporting:** Includes error handling mechanisms to address potential issues during file transfer and report them in a pop-up message.

---

## **Prerequisites**
- **GTK+ Libraries:** Ensure you have GTK+ libraries installed on your system. These libraries provide the graphical user interface functionalities used by the program. You can usually find installation instructions for your specific operating system online.

---

## **Building and Running the Program**

1. **Save the Code:** Save the provided C code as a file with a `.c` extension (e.g., `file_transfer.c`).

2. **Compile the Code:** Compile the code using a C compiler with GTK+ libraries linked. Use the following command:
   ```bash
   gcc file_transfer.c -o file_transfer `pkg-config --cflags --libs gtk+-3.0`
Replace pkg-config --cflags --libs gtk+-3.0 with the appropriate command for your system to link the GTK+ libraries during compilation. Refer to your GTK+ documentation for details.
3. **Run the Program**: Open a terminal window in the directory where you saved the compiled program (file_transfer). Then, execute the program using:
   ```bash
./file_transfer`
