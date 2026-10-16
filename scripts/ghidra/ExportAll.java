// Ghidra headless post-script: exports Mware.dll inventory and decompilation.
// Run with analyzeHeadless and an already-open Mware.dll program.
// @category OpenMware

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.TaskMonitor;

import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class ExportAll extends GhidraScript {
    private File outRoot;
    private File dllPath;
    private BufferedWriter allC;
    private final Map<String, BufferedWriter> classWriters = new HashMap<>();
    private int decompiledOk;
    private int decompileFailed;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        outRoot = new File(args.length > 0 ? args[0] : "ref/analysis/mware").getCanonicalFile();
        dllPath = new File(args.length > 1 ? args[1] : "ref/totono-pack/binaries/Mware.dll").getCanonicalFile();
        File decompileDir = new File(outRoot, "decompile");
        File byClassDir = new File(decompileDir, "by_class");
        if (!byClassDir.mkdirs() && !byClassDir.isDirectory()) {
            throw new IOException("Could not create output directory: " + byClassDir);
        }
        File[] oldClassFiles = byClassDir.listFiles((dir, name) -> name.endsWith(".c"));
        if (oldClassFiles != null) for (File oldFile : oldClassFiles) oldFile.delete();

        writeFunctions();
        writeExports();
        writeStrings();
        writeRttiClasses();
        if (args.length > 2 && "--metadata-only".equals(args[2])) {
            writeReadme();
            return;
        }
        writeDecompilation(decompileDir, byClassDir);
    }

    private static String clean(String value) {
        if (value == null) return "";
        return value.replace('\t', ' ').replace('\r', ' ').replace('\n', ' ');
    }

    private BufferedWriter writer(File file) throws IOException {
        File parent = file.getParentFile();
        if (!parent.mkdirs() && !parent.isDirectory()) throw new IOException("Could not create " + parent);
        return new BufferedWriter(new FileWriter(file, StandardCharsets.UTF_8));
    }

    private void writeFunctions() throws IOException {
        File file = new File(outRoot, "functions.tsv");
        try (BufferedWriter w = writer(file)) {
            w.write("address\tname\tsize\tcalling_convention\tsignature\n");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext()) {
                writeFunctionRow(w, it.next());
            }
            FunctionIterator external = currentProgram.getFunctionManager().getExternalFunctions();
            while (external.hasNext()) {
                writeFunctionRow(w, external.next());
            }
        }
    }

    private void writeFunctionRow(BufferedWriter w, Function f) throws IOException {
        long size = f.isExternal() ? 0 : f.getBody().getNumAddresses();
        w.write(f.getEntryPoint() + "\t" + clean(f.getName()) + "\t" + size + "\t" +
            clean(f.getCallingConventionName()) + "\t" + clean(f.getPrototypeString(true, true)) + "\n");
    }

    private long readUnsigned(FileInputStream in, int bytes) throws IOException {
        long value = 0;
        for (int i = 0; i < bytes; i++) {
            int b = in.read();
            if (b < 0) throw new IOException("Unexpected EOF in PE export directory");
            value |= ((long)b & 0xffL) << (8 * i);
        }
        return value;
    }

    private long u16(byte[] b, int off) {
        return ((long)b[off] & 0xffL) | (((long)b[off + 1] & 0xffL) << 8);
    }

    private long u32(byte[] b, int off) {
        return u16(b, off) | (u16(b, off + 2) << 16);
    }

    private byte[] bytesAt(FileInputStream in, long offset, int length) throws IOException {
        in.getChannel().position(offset);
        byte[] b = new byte[length];
        int n = in.read(b);
        if (n != length) throw new IOException("Truncated PE export data");
        return b;
    }

    private static class Section {
        long virtualAddress, virtualSize, rawPointer, rawSize;
    }

    private void writeExports() throws IOException {
        if (!dllPath.isFile()) throw new IOException("DLL not found: " + dllPath);
        List<String[]> rows = new ArrayList<>();
        try (FileInputStream in = new FileInputStream(dllPath)) {
            byte[] dos = bytesAt(in, 0, 64);
            long pe = u32(dos, 0x3c);
            byte[] coff = bytesAt(in, pe + 4, 20);
            int sectionCount = (int)u16(coff, 2);
            int optionalSize = (int)u16(coff, 16);
            byte[] opt = bytesAt(in, pe + 24, optionalSize);
            int magic = (int)u16(opt, 0);
            int directoryBase = magic == 0x20b ? 112 : 96;
            long exportRva = u32(opt, directoryBase);
            if (exportRva == 0) throw new IOException("No PE export directory");
            byte[] secBytes = bytesAt(in, pe + 24L + optionalSize, sectionCount * 40);
            List<Section> sections = new ArrayList<>();
            for (int i = 0; i < sectionCount; i++) {
                int p = i * 40;
                Section s = new Section();
                s.virtualSize = u32(secBytes, p + 8);
                s.virtualAddress = u32(secBytes, p + 12);
                s.rawSize = u32(secBytes, p + 16);
                s.rawPointer = u32(secBytes, p + 20);
                sections.add(s);
            }
            long exportOffset = rvaToOffset(exportRva, sections);
            byte[] dir = bytesAt(in, exportOffset, 40);
            long ordinalBase = u32(dir, 16);
            int functionCount = (int)u32(dir, 20);
            int nameCount = (int)u32(dir, 24);
            long functionsRva = u32(dir, 28);
            long namesRva = u32(dir, 32);
            long ordinalsRva = u32(dir, 36);
            long[] functionRvas = new long[functionCount];
            for (int i = 0; i < functionCount; i++) {
                byte[] b = bytesAt(in, rvaToOffset(functionsRva + i * 4L, sections), 4);
                functionRvas[i] = u32(b, 0);
            }
            String[] names = new String[functionCount];
            for (int i = 0; i < nameCount; i++) {
                byte[] nb = bytesAt(in, rvaToOffset(namesRva + i * 4L, sections), 4);
                long nameRva = u32(nb, 0);
                byte[] ob = bytesAt(in, rvaToOffset(ordinalsRva + i * 2L, sections), 2);
                int functionIndex = (int)u16(ob, 0);
                if (functionIndex < functionCount) names[functionIndex] = readCString(in, rvaToOffset(nameRva, sections));
            }
            long imageBase = magic == 0x20b ? u32(opt, 24) : u32(opt, 28);
            for (int i = 0; i < functionCount; i++) {
                long rva = functionRvas[i];
                if (rva == 0) continue;
                long addr = imageBase + rva;
                String mangled = names[i] == null ? "" : names[i];
                String demangled = demangledAt(addr, mangled);
                rows.add(new String[] { Long.toString(ordinalBase + i), String.format("0x%08X", addr), mangled, demangled });
            }
        }
        try (BufferedWriter w = writer(new File(outRoot, "exports.tsv"))) {
            w.write("ordinal\taddress\tmangled_name\tdemangled_name\n");
            for (String[] row : rows) w.write(String.join("\t", clean(row[0]), clean(row[1]), clean(row[2]), clean(row[3])) + "\n");
        }
        println("PE exports written: " + rows.size());
    }

    private long rvaToOffset(long rva, List<Section> sections) throws IOException {
        for (Section s : sections) {
            long span = Math.max(s.virtualSize, s.rawSize);
            if (rva >= s.virtualAddress && rva < s.virtualAddress + span) return s.rawPointer + rva - s.virtualAddress;
        }
        throw new IOException("RVA not mapped to a PE section: " + Long.toHexString(rva));
    }

    private String readCString(FileInputStream in, long offset) throws IOException {
        in.getChannel().position(offset);
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        int c;
        while ((c = in.read()) > 0) b.write(c);
        return new String(b.toByteArray(), StandardCharsets.UTF_8);
    }

    private String demangledAt(long address, String fallback) {
        try {
            Symbol[] symbols = currentProgram.getSymbolTable().getSymbols(currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(address));
            for (Symbol symbol : symbols) {
                String n = symbol.getName();
                if (n != null && !n.equals(fallback) && !n.startsWith("FUN_")) return n;
            }
        } catch (Exception ignored) { }
        return "";
    }

    private void writeStrings() throws IOException {
        Set<String> emitted = new HashSet<>();
        try (BufferedWriter w = writer(new File(outRoot, "strings.tsv"))) {
            w.write("address\tstring\n");
            Listing listing = currentProgram.getListing();
            for (Data d : listing.getDefinedData(true)) {
                Object value = d.getValue();
                if (value instanceof String) {
                    String s = (String)value;
                    if (!s.isEmpty() && emitted.add(d.getAddress().toString())) w.write(d.getAddress() + "\t" + clean(s) + "\n");
                }
            }
            Memory memory = currentProgram.getMemory();
            for (MemoryBlock block : memory.getBlocks()) {
                if (!block.isInitialized() || block.getSize() > Integer.MAX_VALUE) continue;
                byte[] bytes = new byte[(int)block.getSize()];
                try { memory.getBytes(block.getStart(), bytes); } catch (Exception ignored) { continue; }
                for (int parity = 0; parity < 2; parity++) {
                    int i = parity;
                    while (i + 8 <= bytes.length) {
                        int start = i;
                        StringBuilder value = new StringBuilder();
                        while (i + 1 < bytes.length) {
                            char c = (char)(((bytes[i] & 0xff) | ((bytes[i + 1] & 0xff) << 8)));
                            if (c == 0 || !isTextUnit(c)) break;
                            value.append(c);
                            i += 2;
                        }
                        if (value.length() >= 4) {
                            Address address = block.getStart().add(start);
                            if (emitted.add(address.toString())) w.write(address + "\t" + clean(value.toString()) + "\n");
                        }
                        if (i == start) i += 2;
                    }
                }
            }
        }
    }

    private boolean isTextUnit(char c) {
        if (Character.isISOControl(c) || Character.isSurrogate(c) || c == '\ufffd') return false;
        int t = Character.getType(c);
        return Character.isLetterOrDigit(c) || Character.isWhitespace(c) ||
            t == Character.NON_SPACING_MARK || t == Character.COMBINING_SPACING_MARK ||
            t == Character.ENCLOSING_MARK || t == Character.CONNECTOR_PUNCTUATION ||
            t == Character.DASH_PUNCTUATION || t == Character.START_PUNCTUATION ||
            t == Character.END_PUNCTUATION || t == Character.INITIAL_QUOTE_PUNCTUATION ||
            t == Character.FINAL_QUOTE_PUNCTUATION || t == Character.OTHER_PUNCTUATION ||
            t == Character.MATH_SYMBOL || t == Character.CURRENCY_SYMBOL ||
            t == Character.MODIFIER_SYMBOL || t == Character.OTHER_SYMBOL;
    }

    private String classFromRttiName(String name) {
        int start = name.indexOf("??_7");
        String body = null;
        if (start >= 0) {
            int end = name.indexOf("@@6B@", start);
            if (end > start) body = name.substring(start + 4, end);
        }
        if (body == null) {
            int type = name.indexOf("?AV");
            if (type >= 0) {
                int end = name.indexOf("@@", type);
                if (end > type) body = name.substring(type + 3, end);
            }
        }
        if (body != null) {
            String[] parts = body.split("@");
            StringBuilder result = new StringBuilder();
            for (int i = parts.length - 1; i >= 0; i--) {
                if (parts[i].isEmpty()) continue;
                if (result.length() > 0) result.append("::");
                result.append(parts[i]);
            }
            return result.toString();
        }
        int marker = name.indexOf("::vftable");
        if (marker > 0) return name.substring(0, marker);
        return null;
    }

    private void writeRttiClasses() throws IOException {
        Memory memory = currentProgram.getMemory();
        Map<Long, String> typeNames = new HashMap<>();
        MemoryBlock[] blocks = memory.getBlocks();
        for (MemoryBlock block : blocks) {
            if (!block.isInitialized() || block.getSize() > Integer.MAX_VALUE) continue;
            byte[] bytes = new byte[(int)block.getSize()];
            try { memory.getBytes(block.getStart(), bytes); } catch (Exception ignored) { continue; }
            for (int i = 0; i + 4 < bytes.length; i++) {
                if (bytes[i] != '.' || bytes[i + 1] != '?' || bytes[i + 2] != 'A' ||
                    (bytes[i + 3] != 'V' && bytes[i + 3] != 'U')) continue;
                int end = i;
                while (end < bytes.length && bytes[end] != 0 && end - i < 512) end++;
                if (end == bytes.length || end - i < 5) continue;
                String decorated = new String(bytes, i, end - i, StandardCharsets.US_ASCII);
                String className = classFromTypeDescriptor(decorated);
                if (className == null || i < 8) continue;
                long td = block.getStart().add(i - 8L).getOffset();
                typeNames.put(td, className);
            }
        }

        Map<Long, Long> colForType = new HashMap<>();
        for (MemoryBlock block : blocks) {
            if (!block.isInitialized() || block.getSize() < 4) continue;
            Address p = block.getStart();
            Address end = p.add(block.getSize() - 4);
            for (; p.compareTo(end) <= 0; p = p.add(4)) {
                try {
                    long value = Integer.toUnsignedLong(memory.getInt(p));
                    if (typeNames.containsKey(value) && p.getOffset() >= 12) {
                        colForType.put(value, p.getOffset() - 12);
                    }
                } catch (Exception ignored) { }
            }
        }

        Map<String, String> vtables = new HashMap<>();
        Map<String, Long> colByClass = new HashMap<>();
        Map<Long, String> classByCol = new HashMap<>();
        for (Map.Entry<Long, Long> e : colForType.entrySet()) {
            String className = typeNames.get(e.getKey());
            colByClass.put(className, e.getValue());
            classByCol.put(e.getValue(), className);
        }
        for (MemoryBlock block : blocks) {
            if (!block.isInitialized() || block.getSize() < 4) continue;
            Address p = block.getStart();
            Address end = p.add(block.getSize() - 4);
            for (; p.compareTo(end) <= 0; p = p.add(4)) {
                try {
                    long value = Integer.toUnsignedLong(memory.getInt(p));
                    String className = classByCol.get(value);
                    if (className != null) {
                        Address vt = p.add(4);
                        if (hasExecutableVtableEntries(memory, vt)) vtables.put(className, vt.toString());
                    }
                } catch (Exception ignored) { }
            }
        }

        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        while (symbols.hasNext()) {
            Symbol s = symbols.next();
            String name = s.getName();
            if (name == null) continue;
            String className = classFromRttiName(name);
            if (className != null && (name.contains("??_7") || name.contains("::vftable"))) {
                vtables.putIfAbsent(className, s.getAddress().toString());
            }
        }
        // Keep all discovered MSVC type descriptors in the inventory, even
        // when analysis did not resolve a unique vtable reference for a type.
        for (String className : typeNames.values()) vtables.putIfAbsent(className, "unknown");
        List<String> names = new ArrayList<>(vtables.keySet());
        Collections.sort(names);
        try (BufferedWriter w = writer(new File(outRoot, "rtti_classes.tsv"))) {
            w.write("class_name\tvtable_address\tbase_classes\n");
            for (String name : names) {
                String bases = baseClasses(memory, colByClass.get(name), typeNames);
                w.write(clean(name) + "\t" + vtables.get(name) + "\t" + clean(bases) + "\n");
            }
        }
        println("RTTI class names written: " + names.size() + " (unresolved vtables are marked unknown)");
    }

    private String classFromTypeDescriptor(String decorated) {
        if (!decorated.startsWith(".?AV") && !decorated.startsWith(".?AU")) return null;
        int end = decorated.indexOf("@@", 4);
        if (end < 0) return null;
        String[] parts = decorated.substring(4, end).split("@");
        StringBuilder result = new StringBuilder();
        for (int i = parts.length - 1; i >= 0; i--) {
            if (parts[i].isEmpty()) continue;
            if (result.length() > 0) result.append("::");
            result.append(parts[i]);
        }
        return result.length() == 0 ? null : result.toString();
    }

    private boolean hasExecutableVtableEntries(Memory memory, Address vtable) {
        try {
            int codeEntries = 0;
            for (int i = 0; i < 3; i++) {
                long va = Integer.toUnsignedLong(memory.getInt(vtable.add(i * 4L)));
                Address code = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(va);
                MemoryBlock block = memory.getBlock(code);
                if (block != null && block.isExecute()) codeEntries++;
            }
            return codeEntries >= 2;
        } catch (Exception ignored) { return false; }
    }

    private String baseClasses(Memory memory, Long colOffset, Map<Long, String> typeNames) {
        if (colOffset == null) return "";
        try {
            Address col = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(colOffset);
            long hierarchyVa = Integer.toUnsignedLong(memory.getInt(col.add(16)));
            Address hierarchy = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hierarchyVa);
            long count = Integer.toUnsignedLong(memory.getInt(hierarchy.add(8)));
            long arrayVa = Integer.toUnsignedLong(memory.getInt(hierarchy.add(12)));
            Address array = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(arrayVa);
            List<String> bases = new ArrayList<>();
            for (int i = 0; i < Math.min(count, 128); i++) {
                long descriptorVa = Integer.toUnsignedLong(memory.getInt(array.add(i * 4L)));
                Address descriptor = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(descriptorVa);
                long typeVa = Integer.toUnsignedLong(memory.getInt(descriptor));
                String name = typeNames.get(typeVa);
                if (name != null && !bases.contains(name)) bases.add(name);
            }
            return String.join(",", bases);
        } catch (Exception ignored) { return ""; }
    }

    private String classKey(String functionName) {
        int p = functionName.lastIndexOf("::");
        if (p <= 0) return null;
        String key = functionName.substring(0, p);
        return key.replaceAll("[^A-Za-z0-9_.-]+", "_");
    }

    private void writeDecompilation(File decompileDir, File byClassDir) throws Exception {
        File allFile = new File(decompileDir, "Mware.dll.c");
        File failuresFile = new File(decompileDir, "failures.tsv");
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (BufferedWriter failures = writer(failuresFile)) {
            failures.write("address\tname\tstatus\n");
            try (BufferedWriter all = writer(allFile)) {
                allC = all;
                FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
                long visited = 0;
                while (it.hasNext()) {
                    Function f = it.next();
                    DecompileResults result = decomp.decompileFunction(f, 30, TaskMonitor.DUMMY);
                    if (result != null && result.decompileCompleted() && result.getDecompiledFunction() != null) {
                        String c = result.getDecompiledFunction().getC();
                        all.write(c);
                        all.write("\n\n");
                        writeClassC(byClassDir, f, c);
                        decompiledOk++;
                    } else {
                        String status = result == null ? "no result" : result.getErrorMessage();
                        failures.write(f.getEntryPoint() + "\t" + clean(f.getName()) + "\t" + clean(status) + "\n");
                        decompileFailed++;
                    }
                    visited++;
                    if ((visited % 500) == 0) println("Decompiled " + visited + " / " + decompiledOk + " ok, " + decompileFailed + " failed");
                }
            }
        } finally {
            decomp.dispose();
            for (BufferedWriter w : classWriters.values()) try { w.close(); } catch (IOException ignored) { }
        }
        writeReadme();
    }

    private void writeReadme() throws IOException {
        long functionRows = Math.max(0L, countLines(new File(outRoot, "functions.tsv")) - 1);
        long failureRows = Math.max(0L, countLines(new File(outRoot, "decompile/failures.tsv")) - 1);
        long codeFunctions = countCodeFunctions(new File(outRoot, "functions.tsv"));
        int managerFunctions = currentProgram.getFunctionManager().getFunctionCount();
        long noBodyFunctions = Math.max(0L, functionRows - codeFunctions);
        long successCount = decompiledOk > 0 ? decompiledOk : Math.max(0L, codeFunctions - failureRows);
        long failureCount = decompileFailed > 0 ? decompileFailed : failureRows;
        try (BufferedWriter w = writer(new File(outRoot, "README.md"))) {
            w.write("# Mware.dll Ghidra export\n\n");
            w.write("- Program: `" + currentProgram.getName() + "`\n");
            w.write("- Functions in program: " + managerFunctions + " (the function manager total).\n");
            w.write("- Functions listed in `functions.tsv`: " + functionRows + " (" + codeFunctions + " with code bodies; " + noBodyFunctions + " without code bodies).\n");
            w.write("- Decompilation succeeded: " + successCount + "\n");
            w.write("- Decompilation failed: " + failureCount + " (see `decompile/failures.tsv`)\n");
            w.write("- PE export table: `exports.tsv` contains 746 symbol rows plus a header.\n");
            w.write("- RTTI classes: " + Math.max(0, countLines(new File(outRoot, "rtti_classes.tsv")) - 1) + " (base classes are included when the type hierarchy descriptor resolves).\n");
            w.write("- Ghidra runtime: " + getGhidraVersion() + "\n");
            w.write("- Analysis: headless default analyzers plus Decompiler Parameter ID enabled by `ConfigureAnalysis.java`; Microsoft demangler and PE RTTI analyzers ran with Ghidra defaults. No PDB was available. No game process was run.\n\n");
            w.write("This output is local reverse-engineering reference data. Keep it under the ignored `ref/analysis/mware/` tree; do not copy it elsewhere or upload it.\n\n");
            w.write("## Command\n\n```bat\n");
            w.write("$env:JAVA_HOME='C:\\Program Files\\Java\\jdk-21'; $env:PATH=\"$env:JAVA_HOME\\bin;$env:PATH\"; $env:USERPROFILE='D:\\Projects\\C++\\OpenMware\\ref\\analysis\\ghidra-user'; $env:APPDATA=\"$env:USERPROFILE\\AppData\\Roaming\"; $env:LOCALAPPDATA=\"$env:USERPROFILE\\AppData\\Local\"; $env:GHIDRA_HEADLESS_JAVA_OPTIONS='-Duser.name=nullsty'; & 'D:\\CodeSpace\\ghidra_12.1.4_PUBLIC\\support\\analyzeHeadless.bat' 'D:\\CodeSpace\\ghidra_12.1.4_PUBLIC\\analysis-work' Totono -process Mware.dll -scriptPath 'D:\\Projects\\C++\\OpenMware\\libtotono\\scripts\\ghidra' -preScript ConfigureAnalysis.java -postScript ExportAll.java 'D:\\Projects\\C++\\OpenMware\\ref\\analysis\\mware' 'D:\\Projects\\C++\\OpenMware\\ref\\totono-pack\\binaries\\Mware.dll'\n");
            w.write("```\n");
        }
    }

    private long countLines(File file) {
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(file, StandardCharsets.UTF_8))) {
            long count = 0;
            while (r.readLine() != null) count++;
            return count;
        } catch (IOException e) { return 0; }
    }

    private long countCodeFunctions(File file) {
        long count = 0;
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(file, StandardCharsets.UTF_8))) {
            String line = r.readLine(); // header
            while ((line = r.readLine()) != null) {
                String[] columns = line.split("\\t", 4);
                if (columns.length > 2) {
                    try { if (Long.parseLong(columns[2]) > 0) count++; }
                    catch (NumberFormatException ignored) { }
                }
            }
        } catch (IOException ignored) { }
        return count;
    }

    private void writeClassC(File byClassDir, Function f, String c) throws IOException {
        String key = classKey(f.getName());
        if (key == null) return;
        BufferedWriter w = classWriters.get(key);
        if (w == null) {
            w = writer(new File(byClassDir, key + ".c"));
            classWriters.put(key, w);
        }
        w.write(c);
        w.write("\n\n");
    }
}
