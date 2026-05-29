// Decompile every function and dump the C source as JSON.
//
// Output: $WW_GHIDRA_OUT/decompiled.json
//
// Entry shape:
//   { "addr": "0x80003150", "name": "OSInit", "code": "void OSInit(void)..." }
//
// This is the slowest pass. Streamed line-by-line as .jsonl first, then
// folded into a single .json object so partial runs survive.
//
// Optional env knobs:
//   WW_DECOMP_MIN_SIZE  - skip functions smaller than this many bytes (default 0)
//   WW_DECOMP_TIMEOUT   - per-function decompile timeout in seconds (default 30)
//
//@category WW
//@runtime Java

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;

public class ExportDecompiled extends GhidraScript {

    private static String jstr(String s) {
        if (s == null) return "\"\"";
        StringBuilder sb = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '\\': sb.append("\\\\"); break;
                case '"':  sb.append("\\\""); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20) sb.append(String.format("\\u%04x", (int)c));
                    else sb.append(c);
            }
        }
        sb.append('"');
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        String outDir = System.getenv("WW_GHIDRA_OUT");
        if (outDir == null || outDir.isEmpty()) {
            outDir = new File(getSourceFile().getAbsolutePath()).getParent() + "/../out";
        }
        File outFile = new File(outDir, "decompiled.json");
        File tmpFile = new File(outDir, "decompiled.jsonl");
        outFile.getParentFile().mkdirs();

        int minSize = parseEnvInt("WW_DECOMP_MIN_SIZE", 0);
        int timeout = parseEnvInt("WW_DECOMP_TIMEOUT", 30);

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();

        FunctionManager fm = currentProgram.getFunctionManager();
        int total = fm.getFunctionCount();
        println("[ExportDecompiled] decompiling " + total + " functions");

        int written = 0, skipped = 0, errors = 0, i = 0;
        try (PrintWriter pw = new PrintWriter(tmpFile)) {
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                if (i++ % 250 == 0) {
                    println("  [" + i + "/" + total + "] " + f.getName());
                }
                int size = (int)f.getBody().getNumAddresses();
                if (size < minSize) { skipped++; continue; }
                String code;
                try {
                    DecompileResults res = decomp.decompileFunction(f, timeout, monitor);
                    if (res == null || !res.decompileCompleted()) { errors++; continue; }
                    code = res.getDecompiledFunction().getC();
                } catch (Exception e) {
                    errors++;
                    continue;
                }
                pw.print("{");
                pw.print("\"addr\": " + jstr(String.format("0x%08X",
                        (int)(f.getEntryPoint().getOffset() & 0xFFFFFFFFL))));
                pw.print(", \"name\": " + jstr(f.getName()));
                pw.print(", \"code\": " + jstr(code));
                pw.println("}");
                written++;
            }
        }

        // Fold .jsonl -> single .json object.
        try (PrintWriter pw = new PrintWriter(outFile);
             BufferedReader br = new BufferedReader(new FileReader(tmpFile))) {
            pw.print("{\"count\": " + written + ", \"functions\": [");
            String line;
            boolean first = true;
            while ((line = br.readLine()) != null) {
                if (line.isEmpty()) continue;
                if (!first) pw.print(",");
                pw.print(line);
                first = false;
            }
            pw.println("]}");
        }
        tmpFile.delete();

        println("[ExportDecompiled] wrote " + written + " entries (skipped=" +
                skipped + " errors=" + errors + ") -> " + outFile.getAbsolutePath());
    }

    private static int parseEnvInt(String key, int dflt) {
        String v = System.getenv(key);
        if (v == null || v.isEmpty()) return dflt;
        try { return Integer.parseInt(v); } catch (NumberFormatException e) { return dflt; }
    }
}
