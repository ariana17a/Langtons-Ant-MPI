Langton's Ant MPI Simulation
Overview

This project implements a simulation of Langton's Ant using C++ and MPI (Message Passing Interface).

Langton's Ant is a two-dimensional cellular automaton that exhibits complex emergent behavior from a simple set of rules. The project includes both a sequential implementation and an MPI-based parallel version designed to demonstrate domain decomposition and inter-process communication.

Features
Sequential simulation mode
MPI parallel simulation
Graphical visualization
Configurable grid size
Configurable number of simulation steps
Support for multiple ants
Real-time statistics
PPM image export
Performance measurements using MPI_Wtime
Langton's Ant Rules

For each step:

If the ant is on a white cell:
Turn right 90°
Flip the cell color
Move forward
If the ant is on a black cell:
Turn left 90°
Flip the cell color
Move forward
Technologies Used
C++17
MPI (OpenMPI / MS-MPI)
SFML (if graphical interface is used)
Git
Running the Project
Sequential Mode
./langton
MPI Mode
mpiexec -n 4 langton.exe
Parameters
Grid Size (N)
Number of Steps (T)
Number of Ants
Performance Evaluation

The project can be used to analyze:

Speedup
Efficiency
Strong Scaling
Weak Scaling
Communication Overhead
Project Structure
main.cpp
README.md
images/
output/
Author

Ariana Apopii

University project – Parallel Algorithms and MPI
