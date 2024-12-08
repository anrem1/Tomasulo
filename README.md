# Tomasulo

Merna Hebishy 900221976

Issues:
Instruction issues after a delay of a clock cycle when the instruction before it is executing.
Some reservation tables are not reflected as in the project (ex: RET and CALL have different reservation stations).
Load/Add sometimes does not update registers which may cause infinite loops if the register is not incremented.
Some simulations display/execute out of order clock cycles.

Note:
ROB doesn't clear after it is committed.
The input isn't traditional assembly or LOAD, it is separated into opcode, three registers, immediate, and offset.
