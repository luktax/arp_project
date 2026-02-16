# arp_project https://github.com/luktax/arp_project

## SKETCH OF THE ARCHITECTURE

File: Architecture.png

## COMPONENTS

## MAIN PROCESS

### initialization

-mode selection STANDALONE|SERVER|CLIENT  
-pipe creation  
-pid creation, fork for all process  
-route table definition, it tells where all the message should be sent  

### initialization SERVER

-socket connection  
-send 'ok'  
-receive 'ook'  

### initialization CLIENT

-socket connection  
-receive 'ok'  
-send 'ook'  
-receive window size  

### loop STANDALONE

-SELECT: read all the message and thanks to the route table, it sends them correctly  
-Check if some message is the "ESC" command, in case block all the process;  
-wait the termination of all the child;  

### loop NETWORK

-define new route table, without watchdog, obstacles and targets.  
(the opponent drone is considered as an obstacle.

### SERVER

-send window size one time  
-send 'drone'  
-send drone position  
-receive 'dok'  
-send 'obst'  
-receive obstacle position  
-send 'pok'  

### CLIENT

-receive 'drone'  
-receive drone position  
-send 'dok'  
-receive 'obst'  
-send obstacle position  
-receive 'pok'  

## BLACKBOARD PROCESS

### initialization

-struct blackboard which contains the drone position, the obstacles and targets position, the dimension of the window and the state.  
-channel of comunication with the father. (write, read)  

### loop

-SELECT: read the massage and based on the source decide what to do:  
-FROM THE KEYBOARD: forward it to the drone process, if it is the ESC command it blocks all the process by forwarding it to everyone;  
-FROM THE DRONE: store the position of the drone and send it to the map;  
-FROM THE MAP: resize message, it has to be forwarded to the obstacles and targets processes;  
-FROM THE OBSTACLES: store the position of the obstacles untill it reaches the number desired, and share it with the map;  
-FROM THE TARGETS: same as the obstacles;  
-CHECK: it checks the distance between the drone and all the obstacles, if they are close, notifies the drone process with the relative distance.  

### network mode

-frequenzy updated to 100Hz  
-only one obstacle (opponent drone)  

## I_KEYBOARD PROCESS

### initialization

-channel of communication with the father  
-ncurses initialization  
-create the initial USER INTERFACE  

### loop

-get the user input and send it to the blackboard;  
-update the interface by showing to the user the key pressed.  

## DRONE PROCESS

### initialization

-struct drone, which stores the position, the velocities, the forces all in two coordinates;  
-struct params, which store the parameter useful to the dynamics of the drone, reading them in the /config/ParameterFile.txt file;  
-channel of communication with father;  
-the drone starts at position x=5,y=5,send to the bb the intial position;  
-intial value of the window size;  

### loop

-load the paramater from the file thanks to the load_params function, this allows us to change the file in real time;  
-check if the user pressed the reset ('r') command, in case reset the struct with the intial values;  
-read the message from the bb: if it is a resize command it updates the window dimension, if it is one of the motion command, update the relative force. If it is an obstacle position, stores it;  
-calculate the dynamics of the drone  
-send the new position to the bb;  

## MAP PROCESS

### initialization

-channel of communication with the father;  
-ncurses intialization;  
-creation of the window;  

### loop

-check if the user is resizing the window, in case it sends the new size to the bb, reset the obstacles and targets arrays;  
-read the message from the bb, if it is the new obstacles and targets position, stores them and set the flag for the redraw at 1, if it is the drone position, stores it;  
-redraw the window.  

## OBSTACLES PROCESS (ONLY STANDALONE MODE)

### initialization

-channel of communication with the father;  
-initial window size;  
-rand init;  

### loop

-read the message, if the bb notified a resize and the window is different from the previous one, set the window_changed flag to true;  
-when the bb sends the STOP message, it stops generating setting the flag to false;  
-if the window has changed it stats generating new obstacle;  

## TARGETS PROCESS (ONLY STANDALONE MODE)

same as obstacles.

## WATCHDOG PROCESS (ONLY STANDALONE MODE)

### initialization

-block the signal receiver to register correctly all the process in the process_pid.log file  
-function signal_handler to handle the signal SIGUSR1 from all the process  

### loop

-register the current time  
-check if all the process are alive, if not notify the user  
-every 1 second, write on the watchdog.log file the current state of the processes.  

## MESSAGE

All the message are sent in a simple struct format, which contains the source id (who sent the message), and the data.  
If a process receives different type of messages, it is used a descripor in the data, such as "O=" "T=" "D=" etc;  

## FOLDER STRUCTURE

/src: all the .c file  
/build: executables (generated by the run.sh script)  
/config: ParameterFile.txt  
/new /include: headers files  
/log: log files  

## HOW TO RUN

In the main folder /arp_project is present a file named "run.sh", it is a simple script, which compiles all the file using the CMakeFile.txt file and then start the program.

To execute:

./run.sh

## COMMAND

The allowable user input are written in the window created by the I_KEYBOARD PROCESS

## Correction of the first assignemnt

The most penalized part is the first one with these comments:

-impossible grab goal  
-no colors  
-robot moves strange  

To solve them I added the color for the targets and the obstacles.  

-I wrote a simple function in the blackboard loop, to check if the robot grab a target, if it is the case it reorders the obstacles arrays and notify the map.  
(line of code: Blackboard.c 339-365)  

-To solve the strange motion of the robot I created a side bar near the map to check the drone dynamics and modified the parameters to get it more maneuverable.  

## third assignment

-socket communication via LAN, following the protocol given.  

## group

-MICHELE TOZZOLA  
-GREGORIO DANERI  
-CHIARA MASIANELLO
