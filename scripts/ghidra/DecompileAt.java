// Ghidra headless post-script: decompiles the functions at the given addresses (creating them when Ghidra has not),
// prints the C to stdout lines prefixed with "DEC>". Usage: -postScript DecompileAt.java 0x4e6cb8 0x4e1234 ...
// @category OpenMware

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        for (String a : getScriptArgs()) {
            Address addr = toAddr(Long.decode(a));
            Function f = getFunctionAt(addr);
            if (f == null) {
                new DisassembleCommand(addr, null, true).applyTo(currentProgram, monitor);
                f = createFunction(addr, null);
            }
            if (f == null) {
                println("DEC> " + a + ": cannot create function");
                continue;
            }
            DecompileResults r = ifc.decompileFunction(f, 60, monitor);
            println("DEC> ==== " + f.getName() + " @ " + f.getEntryPoint());
            if (r.decompileCompleted()) {
                for (String line : r.getDecompiledFunction().getC().split("\n")) println("DEC> " + line);
            } else {
                println("DEC> failed: " + r.getErrorMessage());
            }
        }
    }
}
