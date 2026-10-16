// Enable the Ghidra analyzer options requested by the OpenMware task.
// This script runs before automatic analysis on the copied project.
// @category OpenMware

import ghidra.app.script.GhidraScript;

public class ConfigureAnalysis extends GhidraScript {
    @Override
    protected void run() throws Exception {
        setAnalysisOption(currentProgram, "Decompiler Parameter ID", "true");
        println("Enabled Decompiler Parameter ID; Microsoft demangler and PE RTTI analyzers use Ghidra defaults.");
    }
}
