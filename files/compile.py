SCRIPT_NAME="Mass shader compiler by GliGli"
SCRIPT_VERSION="0.01"

# things you might want to change for a new project

SHADER_COMPILER="./rshadercompiler.exe"
OUTPUT_ASM_FILE="../plugins/drkPvr/shaders.S"
OUTPUT_DIR="./compiled"

SHADER_DEFS= \
[
    # list of shader files
    [
        "vs.hlsl",
        "/vs" # compiler command line args
    ],
    [
        "ps.hlsl",
        "/ps",
        [
            # optional list of defines, with list of possible values per define
            # each combination of those will be a new shader ...
            ["pp_Texture",[0,1]],
            ["pp_Offset",[0,1]],
            ["pp_ShadInstr",[0,1]],
            ["pp_ShadInstr2",[0,1]],
            ["pp_IgnoreTexA",[0,1]],
            ["pp_UseAlpha",[0,1]],
            ["pp_FogCtrl",[0,1]],
            ["pp_FogCtrl2",[0,1]],
            ["pp_Palette",[0,1]],
            ["pp_PaletteBilinear",[0,1]]
        ]
    ]
]

# things that shouldn't be changed  (code)

from md5 import md5
import os, tempfile, subprocess

print "%s %s" % (SCRIPT_NAME,SCRIPT_VERSION)

asmfile=open(OUTPUT_ASM_FILE,"wb")

for shader in SHADER_DEFS:
    
    # step 1 : generate shaders

    print "* Compiling %s" % shader[0]

    fn,ext=os.path.splitext(shader[0])

    shaderfiles=[]

    varsvals=None

    if len(shader)>2:
        varsvals=[0]*len(shader[2])

    looping=True;

    while looping:

        orifile=open(shader[0],"rb")

        tmpfile=open('__tmp__.hlsl',"wb")

        outname=OUTPUT_DIR+'/'+fn

        if len(shader)>2:
            for i in range(len(varsvals)):
                outname=outname+'_%d' % (shader[2][i][1][varsvals[i]])
                tmpfile.write("#define %s %d \r\n" %(shader[2][i][0],shader[2][i][1][varsvals[i]]))

        tmpfile.write(orifile.read())

        tmpfile.close()
        orifile.close()

        outname=os.path.abspath(outname+'.bin')

        out=os.tmpfile()
        subprocess.call(SHADER_COMPILER+" \""+tmpfile.name+"\" \""+outname+"\" "+shader[1],stdout=out)

        out.seek(0,0)
        outs=out.read()

        print outname

        if not '- Compiled!' in  outs: # can't find something better ...
            print outs
            exit(1)

        shaderfiles.append(outname)

        if len(shader)>2:
            varsvals[0]=varsvals[0]+1

            carry=True
            i=0;
            while carry:

                carry=False;
                if varsvals[i]>=len(shader[2][i][1]):
                    if i>=len(varsvals)-1:
                        looping=False
                    else:
                        carry=True;
                        varsvals[i]=0
                        varsvals[i+1]=varsvals[i+1]+1

                i=i+1
        else:
            looping=False

    # step2 : remove duplicates

    indexlist=[]
    shaderdatalist=[]
    shaderhashlist=[]

    for shaderfile in shaderfiles:
        sf=open(shaderfile,"rb")
        sfs=sf.read()
        sfh=md5(sfs).digest()

        if not sfh in shaderhashlist:
            shaderhashlist.append(sfh)
            shaderdatalist.append(sfs)

        indexlist.append(shaderhashlist.index(sfh))

        sf.close()

    print "- %d unique shader(s)" % (len(shaderdatalist))

    # step3 : generate ASM file

        # data
    for i in range(len(shaderdatalist)):
        asmfile.write(".align 4\n")
        asmfile.write("data_%s%d:\n" % (fn,i))

        j=0
        for c in shaderdatalist[i]:
            if (j % 16) == 0:
                asmfile.write("\n\t.byte 0x%02x" % (ord(c)))
            else:
                asmfile.write(", 0x%02x" % (ord(c)))
            j=j+1

        asmfile.write("\n\n")

        # tables
    asmfile.write(".globl %s_table_count\n" % (fn))
    asmfile.write("%s_table_count:\n" % (fn))
    asmfile.write(".long %d\n\n" % (len(shaderdatalist)))

    asmfile.write(".globl %s_data_table\n" % (fn))
    asmfile.write("%s_data_table:\n" % (fn))

    for i in range(len(shaderdatalist)):
        asmfile.write(".long data_%s%d\n" % (fn,i))

    asmfile.write("\n")

    asmfile.write(".globl %s_size_table\n" % (fn))
    asmfile.write("%s_size_table:\n" % (fn))

    for i in range(len(shaderdatalist)):
        asmfile.write(".long %d\n" % (len(shaderdatalist[i])))

    asmfile.write("\n")

        # indices table
    asmfile.write(".globl %s_indice_count\n" % (fn))
    asmfile.write("%s_indice_count:\n" % (fn))
    asmfile.write(".long %d\n\n" % (len(indexlist)))

    asmfile.write(".globl %s_indices\n" % (fn))
    asmfile.write("%s_indices:\n" % (fn))

    for i in indexlist:
        asmfile.write(".long %d\n" % (i))

    asmfile.write("\n")

    print "- ASM generated!"

asmfile.close()

print "- All done!"

exit(0)