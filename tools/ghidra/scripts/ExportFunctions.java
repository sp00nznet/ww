// Export all functions from the current Ghidra program as JSON.
//
// Output: $WW_GHIDRA_OUT/functions.json
//
// Entry shape:
//   { "addr": "0x80003150", "name": "OSInit", "size": 1234,
//     "return_type": "void", "calling_convention": "default",
//     "signature": "void OSInit(void)",
//     "source": "USER_DEFINED" | "DEFAULT" | "ANALYSIS" | "IMPORTED",
//     "params": [{"name":"r3","type":"int"}, ...] }
//
//@category WW
//@runtime Java

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Parameter;

public class ExportFunctions extends GhidraScript {

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
        File outFile = new File(outDir, "functions.json");
        outFile.getParentFile().mkdirs();

        FunctionManager fm = currentProgram.getFunctionManager();
        int total = fm.getFunctionCount();
        println("[ExportFunctions] " + total + " functions");

        try (PrintWriter pw = new PrintWriter(outFile)) {
            pw.print("{\n \"count\": " + total + ",\n \"functions\": [\n");
            boolean first = true;
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                if (!first) pw.print(",\n");
                first = false;

                List<String> paramStrs = new ArrayList<>();
                for (Parameter p : f.getParameters()) {
                    paramStrs.add("{\"name\": " + jstr(p.getName()) +
                                  ", \"type\": " + jstr(p.getDataType().toString()) + "}");
                }

                String cc = f.getCallingConventionName();
                if (cc == null) cc = "default";

                pw.print("  {");
                pw.print(" \"addr\": " + jstr(String.format("0x%08X",
                        (int)(f.getEntryPoint().getOffset() & 0xFFFFFFFFL))));
                pw.print(", \"name\": " + jstr(f.getName()));
                pw.print(", \"size\": " + f.getBody().getNumAddresses());
                pw.print(", \"return_type\": " + jstr(f.getReturnType().toString()));
                pw.print(", \"calling_convention\": " + jstr(cc));
                pw.print(", \"signature\": " + jstr(f.getSignature().toString()));
                pw.print(", \"source\": " + jstr(f.getSymbol().getSource().toString()));
                pw.print(", \"params\": [" + String.join(", ", paramStrs) + "]");
                pw.print(" }");
            }
            pw.print("\n ]\n}\n");
        }
        println("[ExportFunctions] wrote " + total + " entries -> " +
                outFile.getAbsolutePath());
    }
}
