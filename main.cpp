#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <sstream>
using namespace std;

struct InstructionProgress
{
    int issuedCycle = -1;    // Cycle when the instruction was issued
    int startExecCycle = -1; // Cycle when execution started
    int endExecCycle = -1;   // Cycle when execution ended
    int writeCycle = -1;     // Cycle when write-back occurred
    int commitCycle = -1;    // Cycle when the instruction was committed
};

struct Instruction
{
    string opcode;  // Instructions in assembly
    int rA, rB, rC; // Registers
    int imm;        // Immediate value
    string offset;  // Offset for memory operations
    string label;
    InstructionProgress progress;
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
vector<Instruction> instructions;

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
vector<ReservationStation> reservationStations(12);
map<string, int> labelAddresses;

class tomasulo
{
public:
    int totalCycles = 0;
    int instructionsCompleted = 0;
    int branchMispredictions = 0;
    int totalBranches = 0;
    int pc = 0;
    int startingAfdress;

    void initialize();
    void displayMetrics();
    void simulate(vector<Instruction> &instructions, vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer, int startingAddress);
    void issue(Instruction instr, vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer);
    void commit(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob);
    void write(vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer);
    void execute(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob);
    bool allInstructionsCompleted();
    void handleBranch(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob, int mispredictedBranchIndex);
    int allocateROBEntry();
    void setupHardware();
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

    registers[6] = 4;
}

int tomasulo::allocateROBEntry()
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

void tomasulo::displayMetrics()
{
    // cout << "Total Cycles: " << totalCycles << endl;
    cout << "Instructions Per Cycle (IPC): " << (double)instructionsCompleted / totalCycles << endl;
    cout << "Branch Mispredictions: " << branchMispredictions << endl;

    if (totalBranches > 0)
    {
        cout << "Branch Misprediction Rate: "
             << (double)branchMispredictions / totalBranches * 100 << "%" << endl;
    }
    else
    {
        cout << "Branch Misprediction Rate: N/A (No branches encountered)" << endl;
    }

    cout << "\nFinal Register States:\n";
    for (int i = 0; i < registers.size(); ++i)
    {
        cout << "R" << i << " = " << registers[i] << endl;
    }

    // Display Memory Contents
    cout
        << "\nFinal Memory States:\n";
    for (int i = 0; i < memory.size(); ++i)
    {
        cout << "Memory[" << i << "] = " << memory[i] << endl;
    }

    for (const auto &instr : instructions)
    {
        cout << instr.opcode << "   issued: "
             << (instr.progress.issuedCycle == -1 ? "-" : to_string(instr.progress.issuedCycle)) << "   start exec: "
             << (instr.progress.startExecCycle == -1 ? "-" : to_string(instr.progress.startExecCycle)) << "   end exec: "
             << (instr.progress.endExecCycle == -1 ? "-" : to_string(instr.progress.endExecCycle)) << "  write:  "
             << (instr.progress.writeCycle == -1 ? "-" : to_string(instr.progress.writeCycle)) << " commit:  "
             << (instr.progress.commitCycle == -1 ? "-" : to_string(instr.progress.commitCycle)) << endl;
    }
}

void tomasulo::simulate(vector<Instruction> &instructions, vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer, int startingAddress)
{
    int cycle = 0;
    pc = startingAddress; // Initialize program counter with starting address
    instructionsCompleted = 0;

    while (true)
    {
        cycle++;
        totalCycles++;
        cout << "Cycle: " << cycle << ", PC: " << pc << endl;
        // Step 1: Issue stage
        if (pc < instructions.size())
        {
            issue(instructions[pc], reservationStations, reorderBuffer);
            instructions[pc].progress.issuedCycle = cycle;
            pc++; // Move to the next instruction
        }
        cout << "\n\n";
        cycle++;
        totalCycles++;
        cout << "Cycle: " << cycle << ", PC: " << pc << endl;
        // Step 2: Execute stage
        if (instructions[pc - 1].progress.startExecCycle == -1 && totalCycles > instructions[pc - 1].progress.issuedCycle)
        {
            instructions[pc - 1].progress.startExecCycle = totalCycles;
        }
        execute(reservationStations, reorderBuffer);

        // cycle++;
        // cout << "Cycle after execute: " << cycle << ", PC: " << pc << endl;

        // Step 3: Write stage
        // write(reservationStations, reorderBuffer);

        // cycle++;
        // cout << "Cycle after write: " << cycle << ", PC: " << pc << endl;
        // Step 4: Commit stage
        // commit(reservationStations, reorderBuffer);

        // Break condition: Exit when all instructions are completed
        if (allInstructionsCompleted())
        {
            cout << "All instructions completed at cycle: " << totalCycles << endl;
            break;
        }
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
                int offset = stoi(instr.offset);
                rs.address = registers[instr.rB] + offset;
                if (instr.opcode == "STORE")
                {
                    rs.Vj = registers[instr.rA]; // Store value of rA into memory
                    rs.Qj = -1;                  // Assume no dependency for now
                }
                else
                {
                    rs.Vj = registers[instr.rA];
                    rs.Qj = -1; // No dependency for LOAD on this example
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
                rs.Vj = (reorderBuffer[instr.rB].ready ? registers[instr.rB] : 0);
                rs.Qj = (reorderBuffer[instr.rB].ready ? -1 : instr.rB);

                rs.Vk = (instr.opcode == "ADDI") ? instr.imm : (reorderBuffer[instr.rC].ready ? registers[instr.rC] : 0);
                rs.Qk = (instr.opcode == "ADDI") ? -1 : (reorderBuffer[instr.rC].ready ? -1 : instr.rC);
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

void tomasulo::execute(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob)
{
    for (auto &rs : reservationStations)
    {
        cout << "RS op: " << rs.op << ", busy: " << rs.busy << ", resultReady: " << rs.resultReady
             << ", cyclesLeft: " << rs.cyclesLeft << endl;

        if (rs.busy)
        {
            if (rs.Qj != -1) // Operand is not ready, fetch from ROB
            {
                rs.Vj = rob[rs.Qj].value;
            }
            if (rs.Qk != -1) // Operand is not ready, fetch from ROB
            {
                rs.Vk = rob[rs.Qk].value;
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

                if (instructions[pc - 1].progress.endExecCycle == -1)
                {
                    instructions[pc - 1].progress.endExecCycle = totalCycles;
                }
                totalCycles++;

                cout << "\n\n";
                if (instructions[pc - 1].progress.writeCycle == -1 && totalCycles > instructions[pc].progress.endExecCycle)
                {
                    instructions[pc - 1].progress.writeCycle = totalCycles;
                }

                write(reservationStations, rob);
                cout << "\n\n";
                totalCycles++;
                if (instructions[pc - 1].progress.commitCycle == -1 && totalCycles > instructions[pc].progress.writeCycle)
                {
                    instructions[pc - 1].progress.commitCycle = totalCycles;
                }
                commit(reservationStations, rob);
            }
        }
    }
}

void tomasulo::commit(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob)
{
    cout << "Cycle: " << totalCycles << endl;
    for (int i = 0; i < rob.size(); ++i)
    {
        ROBEntry &entry = rob[i];
        cout << "ROB entry " << i << ": state = " << entry.state << ", destination = " << entry.destination
             << ", value = " << entry.value << ", ready = " << entry.ready << endl;

        // Only commit if the instruction is ready and in the "Write" state
        if (entry.ready && entry.state == "Write")
        {
            // Check for branch misprediction (based on linked reservation station)
            if (entry.speculative)
            {
                for (auto &rs : reservationStations)
                {
                    if (rs.robIndex == i && rs.op == "BEQ")
                    {
                        // Check branch result in the reservation station
                        if (rs.result == 1) // Branch was taken (misprediction for always-not-taken predictor)
                        {
                            branchMispredictions++;
                            handleBranch(reservationStations, rob, i);
                            return; // Stop further commits until rollback is handled
                        }
                    }
                }
            }

            // Commit result to destination (for non-branch instructions)
            if (entry.destination != -1) // Valid destination (not for STORE)
            {
                registers[entry.destination] = entry.value;
                cout << "Committed result to R" << entry.destination << ": " << entry.value << endl;
            }

            // Free the associated reservation station
            for (auto &rs : reservationStations)
            {
                if (rs.robIndex == i) // Match the ROB entry
                {
                    rs.busy = false;
                    rs.resultReady = false;
                    rs.robIndex = -1; // Clear the ROB linkage
                    break;
                }
            }

            // Mark ROB entry as committed
            entry.state = "Commit";
            entry.ready = false;
            entry.speculative = false;

            instructionsCompleted++;
        }
    }
}

void tomasulo::write(vector<ReservationStation> &reservationStations, vector<ROBEntry> &reorderBuffer)
{
    cout << "Cycle: " << totalCycles << endl;
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
    // Check reservation stations
    for (const auto &rs : reservationStations)
    {
        if (rs.busy)
            return false; // If any reservation station is busy, instructions are not completed
    }

    // Check ROB
    for (const auto &entry : reorderBuffer)
    {
        if (entry.state != "Empty" && entry.state != "Commit")
            return false; // If any ROB entry is still active, instructions are not completed
    }

    return true; // All instructions are completed
}

void tomasulo::handleBranch(vector<ReservationStation> &reservationStations, vector<ROBEntry> &rob, int mispredictedBranchIndex)
{
    cout << "Branch misprediction detected at ROB entry " << mispredictedBranchIndex << ". Rolling back..." << endl;

    // Reset ROB entries for speculative instructions
    for (int i = mispredictedBranchIndex + 1; i < rob.size(); ++i)
    {
        ROBEntry &entry = rob[i];
        entry.instructionID = -1;
        entry.state = "Empty";
        entry.destination = -1;
        entry.value = 0;
        entry.ready = false;
        entry.speculative = false; // Clear speculative flag
    }

    // Reset all reservation stations for speculative instructions
    for (auto &rs : reservationStations)
    {
        if (rs.robIndex > mispredictedBranchIndex)
        {
            rs.busy = false;
            rs.resultReady = false;
            rs.robIndex = -1;
        }
    }

    // Update PC to the correct branch target
    for (auto &rs : reservationStations)
    {
        if (rs.robIndex == mispredictedBranchIndex && rs.op == "BEQ")
        {
            if (rs.result == 1) // Branch taken
            {
                pc = rs.address; // Correct branch target
                break;
            }
            else
            {
                pc = mispredictedBranchIndex + 1; // Next instruction (not taken)
                break;
            }
        }
    }

    cout << "Rollback complete. Execution resumed from corrected branch." << endl;
}

void loadMemoryFromFile(map<int, int> &memory, const string &filename)
{
    ifstream memoryFile(filename);
    if (!memoryFile)
    {
        cerr << "Error: Could not open memory file!" << endl;
        return;
    }

    int address, value;
    while (memoryFile >> address >> value)
    {
        memory[address] = value; // Load address-value pairs into memory
    }

    memoryFile.close();
    cout << "Memory loaded successfully from file: " << filename << endl;
}

void loadInstructionsFromFile(vector<Instruction> &instructions, const string &filename)
{
    ifstream inputFile(filename);
    if (!inputFile)
    {
        cerr << "Error: Could not open instructions file!" << endl;
        return;
    }

    string line;
    int address = 0;

    // First pass: Store label addresses and instructions
    while (getline(inputFile, line))
    {
        // Skip empty lines
        if (line.empty())
        {
            continue;
        }

        // If the line ends with a colon, it's a label
        if (line.back() == ':')
        {
            string label = line.substr(0, line.length() - 1); // Remove the colon
            labelAddresses[label] = address;                  // Store label and its address
        }
        else
        {
            Instruction instr;
            stringstream ss(line);
            ss >> instr.opcode >> instr.rA >> instr.rB >> instr.rC >> instr.imm >> instr.offset;

            // Check for parsing errors
            if (ss.fail())
            {
                cerr << "Error: Invalid instruction format in file at line: " << line << endl;
                continue;
            }

            // Save the instruction and increment the address
            instructions.push_back(instr);
            address++; // Increment address for each instruction
        }
    }

    // Second pass: Replace labels with their addresses
    for (auto &instr : instructions)
    {
        if (instr.opcode == "BEQ") // Specifically handle BEQ label resolution
        {
            if (labelAddresses.find(instr.offset) != labelAddresses.end())
            {
                instr.imm = labelAddresses[instr.offset]; // Replace label with its address
                instr.offset = "";                        // Clear the label string now that it's replaced
            }
            else if (!instr.offset.empty())
            {
                cerr << "Error: Undefined label " << instr.offset << endl;
            }
        }
        else if (instr.opcode == "CALL") // Handle CALL label resolution
        {
            // Store the return address (next instruction address) in R1
            instr.rA = 1;            // Let's say R1 holds the return address
            instr.imm = address + 1; // The next instruction address is the return address
            if (labelAddresses.find(instr.offset) != labelAddresses.end())
            {
                instr.offset = "";                       // Clear the label name as it's now resolved
                instr.rB = labelAddresses[instr.offset]; // Jump to the label address
            }
            else if (!instr.offset.empty())
            {
                cerr << "Error: Undefined label " << instr.offset << endl;
            }
        }
    }

    inputFile.close();
    cout << "Instructions loaded and parsed successfully from file: " << filename << endl;
}

void tomasulo::setupHardware()
{
    int choice;
    cout << "Would you like to use the default hardware configuration or set up your own?" << endl;
    cout << "1. Default hardware" << endl;
    cout << "2. Custom hardware" << endl;
    cout << "Enter your choice (1 or 2): ";
    cin >> choice;

    if (choice == 1)
    {
        // Default configuration
        availableReservationStations = {
            {"LOAD", 2},
            {"STORE", 1},
            {"BEQ", 1},
            {"CALL", 1},
            {"RET", 1},
            {"ADD", 4},
            {"ADDI", 4},
            {"NAND", 2},
            {"MUL", 1}};

        operationCycles = {
            {"LOAD", 6},
            {"STORE", 6},
            {"BEQ", 1},
            {"CALL", 1},
            {"RET", 1},
            {"ADD", 2},
            {"ADDI", 2},
            {"NAND", 1},
            {"MUL", 8}};
    }
    else if (choice == 2)
    {
        // Custom configuration
        cout << "Enter number of reservation stations for LOAD: ";
        cin >> availableReservationStations["LOAD"];
        cout << "Enter number of reservation stations for STORE: ";
        cin >> availableReservationStations["STORE"];
        cout << "Enter number of reservation stations for BEQ: ";
        cin >> availableReservationStations["BEQ"];
        cout << "Enter number of reservation stations for CALL: ";
        cin >> availableReservationStations["CALL"];
        cout << "Enter number of reservation stations for RET: ";
        cin >> availableReservationStations["RET"];
        cout << "Enter number of reservation stations for ADD: ";
        cin >> availableReservationStations["ADD"];
        cout << "Enter number of reservation stations for ADDI: ";
        cin >> availableReservationStations["ADDI"];
        cout << "Enter number of reservation stations for NAND: ";
        cin >> availableReservationStations["NAND"];
        cout << "Enter number of reservation stations for MUL: ";
        cin >> availableReservationStations["MUL"];

        cout << "Enter number of ROB entries: ";
        int robEntries;
        cin >> robEntries;

        // Initialize ROB with user-defined entries
        vector<ROBEntry> reorderBuffer(robEntries);

        // Now, prompt for the number of cycles for each functional unit
        cout << "Enter number of cycles for LOAD: ";
        cin >> operationCycles["LOAD"];
        cout << "Enter number of cycles for STORE: ";
        cin >> operationCycles["STORE"];
        cout << "Enter number of cycles for BEQ: ";
        cin >> operationCycles["BEQ"];
        cout << "Enter number of cycles for CALL: ";
        cin >> operationCycles["CALL"];
        cout << "Enter number of cycles for RET: ";
        cin >> operationCycles["RET"];
        cout << "Enter number of cycles for ADD: ";
        cin >> operationCycles["ADD"];
        cout << "Enter number of cycles for ADDI: ";
        cin >> operationCycles["ADDI"];
        cout << "Enter number of cycles for NAND: ";
        cin >> operationCycles["NAND"];
        cout << "Enter number of cycles for MUL: ";
        cin >> operationCycles["MUL"];
    }

    // Initialize reservation stations based on available reservation stations
    for (auto &entry : availableReservationStations)
    {
        int numStations = entry.second;
        for (int i = 0; i < numStations; ++i)
        {
            ReservationStation rs;
            rs.op = entry.first;
            rs.busy = false;
            rs.cyclesLeft = operationCycles[entry.first];
            reservationStations.push_back(rs);
        }
    }
}

int main()
{
    // Create an instance of the simulator
    tomasulo simulator;

    // Initialize memory and instructions
    // map<int, int> memory;
    // vector<Instruction> instructions;

    // Step 1: Load memory values from a file
    string memoryFilename;
    cout << "Enter the name of the memory file: ";
    cin >> memoryFilename;
    loadMemoryFromFile(memory, memoryFilename);

    // Step 2: Load program instructions from a file
    string instructionsFilename;
    cout << "Enter the name of the instructions file: ";
    cin >> instructionsFilename;
    loadInstructionsFromFile(instructions, instructionsFilename);

    // Step 3: Ask for the starting address
    int startingAddress;
    cout << "Enter the starting address of the program: ";
    cin >> startingAddress;

    // Step 4: Initialize the simulator with default or user input
    simulator.initialize();
    simulator.setupHardware();

    // Step 5: Execute the simulation
    simulator.simulate(instructions, reservationStations, reorderBuffer, startingAddress);

    return 0;
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
