#include <iostream>
#include <fstream>
#include <map>
#include <vector>
using namespace std;

struct Instruction
{
    string opcode;  // Instructions in assembly
    int rA, rB, rC; // Registers
    int imm;        // Immediate value
    int offset;     // Offset for memory operations
};

struct ReservationStation
{
    string op;
    int Vj, Vk;     // Source operands
    int Qj, Qk;     // Tags for source operands
    int result;     // Computed result
    bool busy;      // Busy flag
    int cyclesLeft; // Remaining execution cycles
    bool resultReady;
    int address;
};

map<string, int> availableReservationStations = {
    {"LOAD", 2},
    {"STORE", 1},
    {"BEQ", 1},
    {"CALL", 1},
    {"RET", 1},
    {"ADD", 4},
    {"ADDI", 4},
    {"NAND", 2},
    {"MUL", 1}};

map<string, int> operationCycles = {
    {"LOAD", 6},
    {"STORE", 6},
    {"BEQ", 1},
    {"CALL", 1},
    {"RET", 1},
    {"ADD", 2},
    {"ADDI", 2},
    {"NAND", 1},
    {"MUL", 8}};

vector<int> registers(8, 0);
map<int, int> memory;
vector<ReservationStation> reservationStations(10); // how many?

class tomasulo
{
public:
    int totalCycles;
    int instructionsCompleted;
    int branchMispredictions;
    int totalBranches;
    void initialize();
    void displayMetrics();
    void simulate(vector<Instruction> &instructions, vector<ReservationStation> &reservationStations);
    void issue(Instruction instr, vector<ReservationStation> &reservationStations);
    void commit(vector<ReservationStation> &reservationStations);
    void write(vector<ReservationStation> &reservationStations);
    void execute(vector<ReservationStation> &reservationStations);
    bool allInstructionsCompleted();
};

void tomasulo::initialize()
{
    for (auto &rs : reservationStations)
    {
        rs.busy = false;
    }
}

void tomasulo::displayMetrics()
{
    cout << "Total Cycles: " << totalCycles << endl;
    cout << "Instructions Per Cycle (IPC): " << (double)instructionsCompleted / totalCycles << endl;
    cout << "Branch Mispredictions: " << branchMispredictions << endl;
    cout << "Branch Misprediction Rate: "
         << (double)branchMispredictions / totalBranches * 100 << "%" << endl;
}

void tomasulo::simulate(vector<Instruction> &instructions, vector<ReservationStation> &reservationStations)
{
    int cycle = 0;
    int pc = 0; // Program counter to track the current instructio
    while (true)
    {

        cycle++;

        // Step 1: Commit stage
        commit(reservationStations);

        // Step 2: Write stage
        write(reservationStations);

        // Step 3: Execute stage
        execute(reservationStations);

        // Step 4: Issue stage
        if (pc < instructions.size())
        {
            issue(instructions[pc], reservationStations);
            pc++; // Move to the next instruction
        }

        // Break condition: Exit when all instructions are completed
        if (allInstructionsCompleted())
            break;
    }

    // Output performance metrics
    displayMetrics();
}

void tomasulo::issue(Instruction instr, vector<ReservationStation> &reservationStations)
{
    // Check if a reservation station is available for the instruction type
    for (auto &rs : reservationStations)
    {
        if (!rs.busy) // Find an available (non-busy) reservation station
        {
            rs.op = instr.opcode;
            rs.busy = true;
            rs.cyclesLeft = operationCycles[instr.opcode]; // Assign remaining cycles
            // Set up operands (e.g., Vj, Vk) and tags (Qj, Qk) based on the instruction
            cout << "Issued instruction: " << instr.opcode << endl;
            return; // Exit after issuing the instruction
        }
    }

    // If no reservation station is available, stall this instruction
    cout << "No available reservation station for instruction: " << instr.opcode << endl;
}

void execute(vector<ReservationStation> &reservationStations)
{
    for (auto &rs : reservationStations)
    {
        if (rs.busy)
        {
            if (rs.cyclesLeft > 0)
            {
                rs.cyclesLeft--; // Decrement cycles
            }
            if (rs.cyclesLeft == 0 && !rs.resultReady)
            {
                // Perform the operation and make result ready
                if (rs.op == "ADD")
                {
                    rs.result = rs.Vj + rs.Vk;
                }
                else if (rs.op == "MUL")
                {
                    rs.result = rs.Vj * rs.Vk;
                }
                else if (rs.op == "LOAD")
                {
                    rs.result = memory[rs.address];
                }
                rs.resultReady = true;
            }
        }
    }
}

void tomasulo::commit(vector<ReservationStation> &reservationStations)
{
    for (auto &rs : reservationStations)
    {
        if (rs.busy && rs.resultReady)
        {
            // Perform the commit operation
            // For example, write back the result to a register or memory
            cout << "Committing instruction: " << rs.op << " with result: " << rs.result << endl;

            // Free the reservation station
            rs.busy = false;
            rs.resultReady = false;
        }
    }
}

void tomasulo::write(vector<ReservationStation> &reservationStations)
{
    for (auto &rs : reservationStations)
    {
        if (rs.busy && rs.cyclesLeft == 0 && !rs.resultReady)
        {
            // Simulate the write stage
            cout << "Writing result for instruction: " << rs.op << endl;
            rs.resultReady = true; // Mark result as ready for commit
        }
    }
}

bool tomasulo::allInstructionsCompleted()
{
    for (const auto &rs : reservationStations)
    {
        if (rs.busy)
            return false; // If any reservation station is busy, instructions are not completed
    }
    return true; // All instructions are completed
}

// memory

// 8 registers, r0 has 0 and cannot be changed

// read instructions from a file
// read data from memory (map) , address and value, 16 bit values

/* 4 backend stages:
    issue: 1 cycle
    execute:
    load: 6
    store: 6
    beq: 1
    call: 1
    add/addi: 2
    nand: 1
    mul: 8

    write: 1
    commit: 1 (except for store)
*/

// not taken predictor for branch

// record number of instructions completed,
// number of conditional branches encountered,
// number of cycles spnned
// number of branch mispredictions

/* display: table listing clock cycle time of each instructions
issue
started execution
finished execution
written
committed */

// total execution time (# of cycles)
// ipc
// branch prediction percentage: branches mispredicted / total branches

// ROB

// reservation stations:

/*
load: 2
store: 1
beq: 1
call/ret: 1
add/addi: 4
nand: 2
mul: 1
*/

// bonus: gui
// implementing and integrating a parser to allow the user to provide the input program using proper
// assembly language including labels for jump targets and function calls. The assembly program can be
// entered directly in the application or in a text file read by the application

int main()
{
}