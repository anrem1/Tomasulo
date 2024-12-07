#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <string>
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
    string op;        // Operation type
    int Vj, Vk;       // Values of source operands
    int Qj, Qk;       // Tags for source operands (dependency management)
    int result;       // Computed result
    bool busy;        // Busy flag
    int cyclesLeft;   // Remaining execution cycles
    bool resultReady; // Result ready for write stage
    int address;      // For LOAD/STORE operations
    int robIndex;     // Associated ROB entry
};

struct ROBEntry
{
    int instructionID; // ID of the instruction in the program
    string state;      // "Issue", "Execute", "Write", "Commit"
    int destination;   // Register or memory address to write to
    int value;         // Computed value
    bool ready;        // Whether the value is ready
    bool speculative;  // Indicates if the instruction was executed speculatively
};
vector<ROBEntry> reorderBuffer(6); // ROB with 6 entries

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
vector<ReservationStation> reservationStations(10); // Example: 10 reservation stations

class tomasulo
{
public:
    int totalCycles = 0;
    int instructionsCompleted = 0;
    int branchMispredictions = 0;
    int totalBranches = 0;
    int pc = 0;

    void initialize();
    void displayMetrics();
    void simulate(vector<Instruction> &instructions, vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer);
    void issue(Instruction instr, vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer);
    void commit(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob);
    void write(vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer);
    void execute(vector<ReservationStation> &reservationStations);
    bool allInstructionsCompleted();
    void handleBranch(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob, int mispredictedBranchIndex);
};

void tomasulo::initialize()
{
    for (auto &rs : reservationStations)
    {
        rs.busy = false;
    }

    for (auto &entry : reorderBuffer)
    {
        entry.instructionID = -1; // No instruction
        entry.state = "Empty";
        entry.destination = -1;
        entry.value = 0;
        entry.ready = false;
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

void tomasulo::simulate(vector<Instruction> &instructions, vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer)
{
    int cycle = 0;
    // int pc = 0; // Program counter to track the current instruction

    while (true)
    {
        cycle++;
        totalCycles++;

        // Step 1: Commit stage
        commit(reservationStations, reorderBuffer);

        // Step 2: Write stage
        write(reservationStations, reorderBuffer);

        // Step 3: Execute stage
        execute(reservationStations);

        // Step 4: Issue stage
        if (pc < instructions.size())
        {
            issue(instructions[pc], reservationStations, reorderBuffer);
            pc++; // Move to the next instruction
        }

        // Break condition: Exit when all instructions are completed
        if (allInstructionsCompleted())
            break;
    }

    // Output performance metrics
    displayMetrics();
}

void tomasulo::issue(Instruction instr, vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer)
{
    // Step 1: Allocate ROB entry
    int robIndex = allocateROBEntry();
    if (robIndex == -1)
    {
        cout << "ROB full, cannot issue instruction: " << instr.opcode << endl;
        return;
    }

    // Step 2: Find an available reservation station
    for (auto &rs : reservationStations)
    {
        if (!rs.busy) // Find an available (non-busy) reservation station
        {
            rs.op = instr.opcode;
            rs.busy = true;
            rs.robIndex = robIndex;                        // Link to ROB entry
            rs.cyclesLeft = operationCycles[instr.opcode]; // Assign remaining cycles

            // Step 3: Handle operands and dependencies
            if (instr.opcode == "LOAD" || instr.opcode == "STORE")
            {
                rs.address = registers[instr.rB] + instr.offset;
                if (instr.opcode == "STORE")
                {
                    rs.Vj = registers[instr.rA]; // Store value of rA into memory
                    rs.Qj = -1;                  // Assume no dependency for now
                }
            }
            else if (instr.opcode == "BEQ")
            {
                rs.Vj = registers[instr.rA];
                rs.Qj = -1; // Assume operand is ready
                rs.Vk = registers[instr.rB];
                rs.Qk = -1;                                 // Assume operand is ready
                reorderBuffer[robIndex].speculative = true; // Mark speculative
            }
            else if (instr.opcode == "CALL")
            {
                rs.result = pc + 1 + instr.imm;            // Compute return address
                reorderBuffer[robIndex].value = rs.result; // Save return address
                reorderBuffer[robIndex].ready = true;      // Mark ready
            }
            else if (instr.opcode == "RET")
            {
                rs.result = registers[1]; // Return to address stored in R1
            }
            else
            {
                // Other ALU operations
                rs.Vj = reorderBuffer[instr.rB].ready ? registers[instr.rB] : 0;
                rs.Qj = reorderBuffer[instr.rB].ready ? -1 : instr.rB;

                rs.Vk = (instr.opcode == "ADDI") ? instr.imm : reorderBuffer[instr.rC].ready ? registers[instr.rC]
                                                                                             : 0;
                rs.Qk = (instr.opcode == "ADDI") ? -1 : reorderBuffer[instr.rC].ready ? -1
                                                                                      : instr.rC;
            }

            // Step 4: Initialize ROB entry
            reorderBuffer[robIndex].instructionID = instr.rA;
            reorderBuffer[robIndex].destination = instr.rA; // Assume rA is the destination
            reorderBuffer[robIndex].state = "Issue";
            reorderBuffer[robIndex].ready = false;

            // Set speculative flag for branch-related instructions
            reorderBuffer[robIndex].speculative = (instr.opcode == "BEQ" || instr.opcode == "CALL" || instr.opcode == "RET");

            cout << "Issued instruction: " << instr.opcode << " to ROB entry " << robIndex << endl;
            return; // Exit after issuing the instruction
        }
    }

    // If no reservation station is available, stall this instruction
    cout << "No available reservation station for instruction: " << instr.opcode << endl;
}

void tomasulo::execute(vector<ReservationStation> &reservationStations)
{
    for (auto &rs : reservationStations)
    {
        if (rs.busy)
        {
            // Wait for operands if not ready
            if (rs.Qj != -1 || rs.Qk != -1)
            {
                continue; // Wait for operands to arrive on the CDB
            }

            if (rs.cyclesLeft > 0)
            {
                rs.cyclesLeft--; // Decrement cycles
            }

            if (rs.cyclesLeft == 0 && !rs.resultReady)
            {
                // Perform the operation based on the instruction type
                if (rs.op == "ADD")
                {
                    rs.result = rs.Vj + rs.Vk;
                }
                else if (rs.op == "ADDI")
                {
                    rs.result = rs.Vj + rs.Vk; // Vk is the immediate value
                }
                else if (rs.op == "NAND")
                {
                    rs.result = ~(rs.Vj & rs.Vk);
                }
                else if (rs.op == "MUL")
                {
                    rs.result = rs.Vj * rs.Vk;
                }
                else if (rs.op == "LOAD")
                {
                    rs.result = memory[rs.address];
                }
                else if (rs.op == "STORE")
                {
                    memory[rs.address] = rs.Vj; // Store value into memory
                }
                else if (rs.op == "BEQ")
                {
                    totalBranches++;
                    if (rs.Vj == rs.Vk) // Branch condition
                    {
                        rs.result = 1; // Indicate branch taken
                    }
                    else
                    {
                        rs.result = 0; // Indicate branch not taken
                    }
                }
                else if (rs.op == "CALL")
                {
                    rs.result = pc + 1; // Compute return address
                }
                else if (rs.op == "RET")
                {
                    rs.result = registers[1]; // Return to address in R1
                }

                // Mark the result as ready
                rs.resultReady = true;
            }
        }
    }
}

void tomasulo::commit(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob)
{
    for (int i = 0; i < rob.size(); ++i) // Traverse ROB in program order
    {
        ROBEntry &entry = rob[i];

        // Only commit if the instruction is ready and in the "Write" state
        if (entry.ready && entry.state == "Write")
        {
            // Step 1: Write result to destination
            if (entry.destination != -1) // Valid destination (not for STORE)
            {
                registers[entry.destination] = entry.value;
                cout << "Committed result to R" << entry.destination << ": " << entry.value << endl;
            }

            // Step 2: Free the associated reservation station
            for (auto &rs : reservationStations)
            {
                if (rs.robIndex == i) // Match the ROB entry
                {
                    rs.busy = false;
                    rs.resultReady = false;
                    break;
                }
            }

            // Step 3: Mark ROB entry as committed
            entry.state = "Commit";
            entry.instructionID = -1;
            entry.ready = false;

            // Step 4: Handle speculative execution (rollback not shown here)
            if (entry.speculative)
            {
                // Placeholder: Handle branch misprediction rollback logic
                // (This might involve resetting ROB and RS entries).
                cout << "Speculative instruction committed (pending rollback checks)." << endl;
            }

            // Update metrics
            instructionsCompleted++;
        }
    }
}

void tomasulo::write(vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer)
{
    for (auto &rs : reservationStations)
    {
        if (rs.busy && rs.resultReady)
        {
            // Write result to ROB
            reorderBuffer[rs.robIndex].value = rs.result;
            reorderBuffer[rs.robIndex].ready = true;
            reorderBuffer[rs.robIndex].state = "Write";

            // Broadcast result on the CDB
            for (auto &rsWaiting : reservationStations)
            {
                if (rsWaiting.Qj == rs.robIndex)
                {
                    rsWaiting.Vj = rs.result;
                    rsWaiting.Qj = -1;
                }
                if (rsWaiting.Qk == rs.robIndex)
                {
                    rsWaiting.Vk = rs.result;
                    rsWaiting.Qk = -1;
                }
            }

            // Free reservation station
            rs.busy = false;
            rs.resultReady = false;

            cout << "Wrote result for instruction in ROB entry " << rs.robIndex << endl;
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

void tomasulo::handleBranch(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob, int mispredictedBranchIndex)
{
    cout << "Branch misprediction detected at ROB entry " << mispredictedBranchIndex << ". Rolling back..." << endl;

    // Invalidate all speculative instructions in the ROB
    for (int i = mispredictedBranchIndex; i < rob.size(); ++i)
    {
        ROBEntry &entry = rob[i];
        if (entry.speculative)
        {
            entry.instructionID = -1;
            entry.state = "Empty";
            entry.destination = -1;
            entry.value = 0;
            entry.ready = false;
        }
    }

    // Reset all reservation stations associated with speculative instructions
    for (auto &rs : reservationStations)
    {
        if (rs.robIndex >= mispredictedBranchIndex)
        {
            rs.busy = false;
            rs.resultReady = false;
        }
    }

    cout << "Rollback complete. Execution resumed from corrected branch." << endl;
}

int allocateROBEntry()
{
    for (int i = 0; i < reorderBuffer.size(); ++i)
    {
        if (reorderBuffer[i].state == "Empty")
        {
            return i;
        }
    }
    return -1; // No available ROB entry
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

/*
algorithm steps:
Issue—get instruction from FP Op Queue
• If reservation station free and there is an empty slot in the ROB, send the operands
to the reservation station if they are available in either the registers or the ROB. The
number of the ROB entry is also sent to the reservation station.
• Execute—operate on operands (EX)
• When both operands are ready then execute; if not ready, watch Common Data Bus for result
• Write result—finish execution (WB)
• Write the result (along with the ROB tag) on Common Data Bus to all awaiting units
and ROB
• Mark the reservation station as available
• Commit—When the instruction is no longer speculative
• When an instruction reaches the head of the ROB and its result is available,
the processor updates the register file (or the memory in case of a store) and removes the
instruction from the ROB.
• In case the instruction is a branch who has been incorrectly predicted, the ROB is
flushed and execution is restarted from the correct branch target



*/