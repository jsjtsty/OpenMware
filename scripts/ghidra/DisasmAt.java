// Ghidra headless post-script: prints the instructions of the functions at the given addresses ("ASM>" lines), creating
// the function when Ghidra has none. An argument "f:0x5be850" prints the float and double stored there instead.
// Usage: -postScript DisasmAt.java 0x535820 f:0x5be850 ...
// @category OpenMware

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

public class DisasmAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        for (String a : getScriptArgs()) {
            if (a.startsWith("f:")) {
                Address d = toAddr(Long.decode(a.substring(2)));
                println("ASM> data " + d + " float=" + getFloat(d) + " double=" + getDouble(d) + " int=" + getInt(d));
                continue;
            }
            Address addr = toAddr(Long.decode(a));
            Function f = getFunctionAt(addr);
            if (f == null) {
                new DisassembleCommand(addr, null, true).applyTo(currentProgram, monitor);
                f = createFunction(addr, null);
            }
            if (f == null) {
                println("ASM> " + a + ": no function");
                continue;
            }
            println("ASM> ==== " + f.getName() + " @ " + f.getEntryPoint());
            Instruction ins = getInstructionAt(f.getEntryPoint());
            while (ins != null && f.getBody().contains(ins.getAddress())) {
                println("ASM> " + ins.getAddress() + "  " + ins);
                ins = ins.getNext();
            }
        }
    }
}
