// Export non-function symbols (globals, labels, data) as JSON.
//
// Output: $WW_GHIDRA_OUT/symbols.json
//
// Function symbols are excluded — those live in functions.json.
// Default labels (LAB_xxxx, DAT_xxxx, FUN_xxxx, SUB_xxxx) are excluded.
//
//@category WW
//@runtime Java

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.SymbolType;
import ghidra.program.model.symbol.SourceType;

public class ExportSymbols extends GhidraScript {

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
        File outFile = new File(outDir, "symbols.json");
        outFile.getParentFile().mkdirs();

        SymbolTable st = currentProgram.getSymbolTable();
        SymbolIterator it = st.getAllSymbols(true);

        try (PrintWriter pw = new PrintWriter(outFile)) {
            pw.print("{\n \"symbols\": [\n");
            int n = 0;
            boolean first = true;
            while (it.hasNext()) {
                Symbol s = it.next();
                if (s.getSymbolType() == SymbolType.FUNCTION) continue;
                SourceType src = s.getSource();
                if (src == SourceType.DEFAULT) continue;
                String name = s.getName();
                if (name.startsWith("LAB_") || name.startsWith("DAT_") ||
                    name.startsWith("FUN_") || name.startsWith("SUB_") ||
                    name.startsWith("switchD_") || name.startsWith("caseD_")) continue;

                if (!first) pw.print(",\n");
                first = false;

                pw.print("  {");
                pw.print(" \"addr\": " + jstr(String.format("0x%08X",
                        (int)(s.getAddress().getOffset() & 0xFFFFFFFFL))));
                pw.print(", \"name\": " + jstr(name));
                pw.print(", \"namespace\": " + jstr(s.getParentNamespace().getName()));
                pw.print(", \"type\": " + jstr(s.getSymbolType().toString()));
                pw.print(", \"source\": " + jstr(src.toString()));
                pw.print(" }");
                n++;
            }
            pw.print("\n ],\n \"count\": " + n + "\n}\n");
            println("[ExportSymbols] wrote " + n + " entries -> " +
                    outFile.getAbsolutePath());
        }
    }
}
