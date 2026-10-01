// SPDX-License-Identifier: GPL-3.0-or-later
// Reproducible source build with the locally installed JPEXS compiler library.
import java.nio.file.*;
import java.io.*;
import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.types.RECT;
import com.jpexs.decompiler.flash.configuration.Configuration;
import com.jpexs.decompiler.flash.abc.avm2.parser.script.ActionScript3Parser;
import com.jpexs.decompiler.flash.abc.avm2.parser.script.AbcIndexing;

public final class CompileClipboardInput {
    public static void main(String[] args) throws Exception {
        if (args.length != 3) throw new IllegalArgumentException("source output playerglobal.swc");
        Configuration.playerLibLocation.set(args[2]);
        SWF.initPlayer();
        SWF swf = new SWF();
        swf.version = 10;
        swf.displayRect = new RECT(0, 25600, 0, 14400);
        swf.frameRate = 30; swf.frameCount = 1;
        FileAttributesTag attributes = new FileAttributesTag(swf);
        attributes.actionScript3 = true;
        swf.addTag(attributes);
        DoABC2Tag abc = new DoABC2Tag(swf);
        abc.flags = 1;
        swf.addTag(abc);
        AbcIndexing index = new AbcIndexing(swf, SWF.getPlayerGlobalAbcIndex());
        index.selectAbc(abc.getABC());
        ActionScript3Parser parser = new ActionScript3Parser(index);
        parser.addScript(Files.readString(Path.of(args[0])), "ClipboardInput.as", 0, 0, "ClipboardInput", abc.getABC());
        SymbolClassTag symbols = new SymbolClassTag(swf);
        symbols.tags.add(0); symbols.names.add("ClipboardInput");
        swf.addTag(symbols); swf.addTag(new ShowFrameTag(swf));
        try (OutputStream out = Files.newOutputStream(Path.of(args[1]))) { swf.saveTo(out); }
        SWF verify = new SWF(Files.newInputStream(Path.of(args[1])), false);
        if (!"ClipboardInput".equals(verify.getDocumentClass()) || verify.getAS3Packs().size() != 1)
            throw new IllegalStateException("Compiled input asset contract failed");
        System.out.println("ClipboardInput protocol 1 compiled: " + Files.size(Path.of(args[1])) + " bytes");
        System.exit(0);
    }
}
