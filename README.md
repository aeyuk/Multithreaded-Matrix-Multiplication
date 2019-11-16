# Multithreaded Matrix Multiplication

## Description

This project codes a multithreaded reader/writer system that multiples matrices using ipc message queues.
It includes two programs: package and compute.

* *Package*: creates and populates the input matrices from a pair of input files.
Creates a thread to package up dot product subtasks (one row * one column). 
Each subtask will be put on the message queue as message *type 1*. 
Once package is done creating subtask packaging threads, 
it will read the completed calculations from the queue and safely populate the output matrix. 
Once all the calculations are completed, the package will print the output matrix to the screen and the output file and exit.

* *Compute*: creates a pool of threads to read the subtasks from the message queue, 
calculate the value, complete the calculation and put the result back on the message queue with a message *type 2*.

Each program will capture a `SIGINT`. 
When a `SIGINT` is received (Ctl-C), each program will print out a status of the calculation. 
Ctl-\ will need to be used to terminate program.
`Jobs Sent 5 Jobs Received 0`

Each program will print a message when a message is successfully sent of received.
`Sending job id 12 type 1 size 48 (rc=0)` 
`Receiving job id 12 type 1 size 48`

## Input Files
There are two input files. One file for each input matrix with integers space delimted.

<matrix1.dat>
`3 4
1 2 3 4 5 6 7 8 9 10 11 12`

<matrix2.dat>
`4 5
1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20`

## Output File
One file for the output matrix with integers space delimited.
<output.dat>
`110 120 130 140 150 150 246 272 298 324 350 382 424 466 508 550`

## How to Execute:
Compile the program using `make`. Open two terminal windows: one to run package and one to run compute.
To run *package*: `./package matrix1.dat matrix2.dat m_output.dat 3`
To run *compute*: `./compute <thread pool size>` (add argument `-n` to just read and output calculations).
Use `Ctrl-C` to view counts for jobs sent and received.
Use `Ctrl-\' to quit the program. (compute will run endlessly).
