// Exports local C-like reconstructions and instruction listings from supplied binaries.
// @category RigKontrol3.ReverseEngineering
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.framework.Application;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;

public class DecompileDriver extends GhidraScript {
    private static String safe(String value) {
        String result=value.replaceAll("[^a-zA-Z0-9_.-]", "_");
        return result.length()>140 ? result.substring(0,140) : result;
    }
    private static String clean(String value) {
        return value==null ? "" : value.replace('\t',' ').replace('\n',' ').replace('\r',' ');
    }
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length!=1) throw new IllegalArgumentException("DecompileDriver.java OUTPUT_DIRECTORY");
        Path output=Paths.get(args[0]); Files.createDirectories(output);
        DecompInterface decompiler=new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        decompiler.setSimplificationStyle("decompile");
        if(!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
        List<String> index=new ArrayList<>();
        index.add("address\tname\tstatus\tfile\tdiagnostic");
        int functions=0, completed=0;
        try {
            FunctionIterator iterator=currentProgram.getFunctionManager().getFunctions(true);
            while(iterator.hasNext() && !monitor.isCancelled()) {
                Function function=iterator.next();
                MemoryBlock block=currentProgram.getMemory().getBlock(function.getEntryPoint());
                if(function.isExternal() || block==null || !block.isInitialized() || !block.isExecute()) continue;
                functions++;
                String address=function.getEntryPoint().toString();
                String name=function.getName(true);
                String stem=address+"_"+safe(name);
                DecompileResults result=decompiler.decompileFunction(function,45,monitor);
                boolean ok=result.decompileCompleted() && result.getDecompiledFunction()!=null;
                String diagnostic=clean(result.getErrorMessage());
                if(ok) {
                    String text="/* Input: "+currentProgram.getName()+"; address: "+address+"; symbol: "+name+"\n"
                        +" * Machine-code reconstruction; recovered types/control flow require verification.\n */\n"
                        +result.getDecompiledFunction().getC();
                    Files.writeString(output.resolve(stem+".c"),text,StandardCharsets.UTF_8);
                    completed++;
                }
                StringBuilder listing=new StringBuilder();
                InstructionIterator instructions=currentProgram.getListing().getInstructions(function.getBody(),true);
                while(instructions.hasNext()) {
                    Instruction instruction=instructions.next();
                    listing.append(instruction.getAddress()).append('\t').append(instruction).append('\n');
                }
                Files.writeString(output.resolve(stem+".asm"),listing.toString(),StandardCharsets.UTF_8);
                index.add(address+"\t"+clean(name)+"\t"+(ok?"decompiled":"failed")+"\t"+stem+"\t"+diagnostic);
            }
        } finally { decompiler.dispose(); }
        Files.write(output.resolve("functions.tsv"),index,StandardCharsets.UTF_8);
        String digest=currentProgram.getExecutableSHA256();
        if(digest==null || digest.isBlank()) throw new IllegalStateException("Imported program has no SHA-256 provenance");
        Path input=Paths.get(currentProgram.getExecutablePath());
        if(Files.isRegularFile(input)) {
            byte[] hash=MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(input));
            String currentDigest=HexFormat.of().formatHex(hash);
            if(!digest.equalsIgnoreCase(currentDigest))
                throw new IllegalStateException("Analyzed program SHA-256 differs from current source file");
        }
        List<String> manifest=List.of(
            "tool=Ghidra "+Application.getApplicationVersion(),
            "input="+currentProgram.getExecutablePath(),
            "input.sha256="+digest,
            "language="+currentProgram.getLanguageID(),
            "compiler="+currentProgram.getCompilerSpec().getCompilerSpecID(),
            "functions="+functions,
            "decompiled="+completed,
            "cancelled="+monitor.isCancelled());
        Files.write(output.resolve("manifest.txt"),manifest,StandardCharsets.UTF_8);
        println("RK3_DECOMPILE functions="+functions+" decompiled="+completed+" output="+output);
    }
}
