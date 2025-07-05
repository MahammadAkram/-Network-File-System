
# 📜 README: Your Distributed File System

## 🚀 HOW TO RUN
1. **Run the Naming Server:**  
   ```bash
   make ns
   ```  
2. **Run the Client:**  
   For local server:  
   ```bash
   make cl
   ```  
   For any other setup, just replace `./client <IPOFSERVER>` with your custom command.
3. **Run the SS:**  
   For local server:  
   ```bash
   make ss
   ```  
   For any other setup, just replace `./storage_server <IP OF SERVER> <ROOT_PATH>` with your custom command.
4. **NB:**`rm -rf ./backupfolerforss` to remove the backfolders in each maching in SS 

---

## ✨ BASIC OVERVIEW OF COMMANDS

1. **File Operations (`ls`, `write`, `cat`)**:  
   These commands query the Naming Server (NM) for the IP of the Storage Server (SS), then communicate directly with the SS.

2. **Write Async (`write` command)**:  
   The client specifies a port where it will listen for asynchronous command acknowledgments (ACKs).

3. **Directory and File Management (`mkdir`, `touch`, `rmdir`, `rm`)**:  
   These commands are sent to the NM, which processes them, communicates with the appropriate SS, and sends the output back to the client.

4. **Streaming (`stream` command)**:  
   This command uses `ffplay` to work. Please ensure it is installed on your system.
5. **Backup and Redundancy**: Backup and redundancy mechanisms work in conjunction with caching.

---

## 🛠️ ASSUMPTIONS

1. **Maximum Storage Servers**: Up to 10 servers (tested with 5).  
2. **Maximum Clients**: Unlimited.  
3. **Path Restriction**: All accessible paths must be within the directory where the code is running.  
4. **Buffer Size**: Default buffer size is 2048 bytes; maximum file size is 4096 bytes (can be adjusted in `helper.h`).  
5. **Ports**: Storage Server and Client ports are assigned automatically, allowing multiple instances to run on the same machine.  
6. **Unique Paths**: No SS should have the same root folder as the base accessible directory.  
7. **Backups**: All backups are stored in the `backupforss` folder. This folder should not be used as an accessible path.
8. Root Folder Protection: Do not copy or remove root folders.
9. **User Assumptions**: We trust that users will treat the code with love and care. ❤️  

---

## ❌ LIMITATIONS

1. **Buffer Flow Issues**: There are minor buffer flow issues that are challenging to debug but do not affect functionality in most cases.  
2. **Confidence**: We believe our code is fantastic, but perfection is a journey. 😉  
