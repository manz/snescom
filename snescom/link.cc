#include <cstdio>
#include <vector>
#include <string.h>
using namespace std;

#include "o65linker.hh"
#include "msginsert.hh"
#include "dataarea.hh"
#include "space.hh"

#include "object.hh"

#include <getopt.h>

bool already_reprocessed = true; /* required by object.cc, not really used */
bool assembly_errors = false;
int address_type = 3; /* Set Highrom by default */
unsigned RomSize = 0;
char *freespacefile;

namespace
{
    enum OutputFormat
    {
        IPSformat,
        O65format,
        RAWformat,
        SMCformat
    } format = IPSformat;

    void SetOutputFormat(const std::string& s)
    {
        if(s == "ips") format = IPSformat;
        else if(s == "o65") format = O65format;
        else if(s == "raw") format = RAWformat;
        else if(s == "smc") format = SMCformat;
        else
        {
            std::fprintf(stderr, "Error: Unknown output format %s'\n", s.c_str());
        }
    }
}

void MessageLinkingModules(unsigned count)
{
    //printf("Linking %u modules\n", count);
}

void MessageLoadingItem(const string& header)
{
    //printf("Loading %s\n", header.c_str());
}

void MessageWorking()
{
}

void MessageDone()
{
    //printf("- done\n");
}

void MessageModuleWithoutAddress(const string& name, const SegmentSelection seg)
{
    fprintf(stderr, "O65 linker: Module %s is still without address for seg %s\n",
        name.c_str(), GetSegmentName(seg).c_str());
}

void MessageUndefinedSymbol(const string& name)
{
    fprintf(stderr, "O65 linker: Symbol '%s' still undefined\n", name.c_str());
}

void MessageDuplicateDefinition(const string& name, unsigned nmods, unsigned ndefs)
{
    fprintf(stderr, "O65 linker: Symbol '%s' defined in %u module(s) and %u global(s)\n",
        name.c_str(), nmods, ndefs);
}

void MessageUndefinedSymbols(unsigned n)
{
    fprintf(stderr, "O65 linker: Still %u undefined symbol(s)\n", n);
}

static void LoadFreespaceSpecs(freespacemap& freespace)
{
    // Assume everything is free space!
    /* FIXME: Make this configurable. */
    // add a file mapping ?
    // something like:
    // address : size
    for(unsigned page=0xC0; page<=0xFF; ++page)
        freespace.Add(page, 0x0000, GetPageSize());
    
    freespace.Del(0x00, 0x0000, 0x8000); // Not usable
}

static unsigned Calc2pow(unsigned ROMsize)
{
    unsigned result = 0;
    while( (1ULL << result) < ROMsize) ++result;
    return result;
}

static void WriteCheckSumPair(std::FILE* stream, unsigned HeaderBegin, unsigned sum1)
{
    sum1 &= 0xFFFF;
    unsigned sum2 = sum1 ^ 0xFFFF;

    fseek(stream, HeaderBegin + 0xDC, SEEK_SET);
    fputc(sum2 & 0xFF, stream);
    fputc(sum2 >> 8, stream);
    fputc(sum1 & 0xFF, stream);
    fputc(sum1 >> 8, stream);
}

static void MapSNESintoROM(DataArea& area, const Object& obj)
{
    unsigned base = obj.GetSegmentBase();
    unsigned size = obj.GetSegmentSize();
    
    //fprintf(stderr, "base=%u, size=%u\n", base,size);
    
    while(size > 0)
    {
        unsigned base_begin = base - (base % GetPageSize());
        unsigned base_end   = base_begin + GetPageSize();
        
        unsigned write_to    = SNES2ROMaddr(base);
        unsigned write_count = size;
        if(base + write_count > base_end) write_count = base_end - base;

        fprintf(stderr, "  base=$%X, size=$%X, write_to=$%X, write_count=$%X\n",
            base, size, write_to, write_count);
        
        area.WriteLump(write_to,
           obj.GetContent(base, write_count));
        
        base += write_count;
        size -= write_count;
    }
}

static void FixupSMC(const Object& obj, std::FILE* stream)
{
    DataArea rom_obj;
    MapSNESintoROM(rom_obj, obj);

Do_Over:
    unsigned filesize = rom_obj.GetTop();
    if(filesize < RomSize) filesize = RomSize;
    std::vector<unsigned char> ROMdata = rom_obj.GetContent(0, filesize);
    
    fseek(stream, 0, SEEK_SET);
    fwrite(&ROMdata[0], 1, ROMdata.size(), stream);
    
    unsigned RomSize2pow = Calc2pow(filesize);
    if(RomSize2pow < 10) RomSize2pow = 10;
    unsigned RomSizeSmallPow = RomSize2pow - 10;
    unsigned CalculatedSize = 1 << RomSize2pow;
    
    unsigned Pow2SizeDown = filesize;
    if(CalculatedSize > Pow2SizeDown) Pow2SizeDown = 1 << (RomSize2pow-1);
    
    unsigned HeaderOffs = GuessROMheaderOffset(&ROMdata[0], CalculatedSize);
    unsigned HeaderBegin = HeaderOffs & 0xFFFF00;
    unsigned HeaderUsage = rom_obj.GetUtilization(HeaderBegin + 0xB0, 0x50);
    unsigned MinimumUtilization =
        3*2 // 3 vectors
       +3   // misc vars
       ;
    if(HeaderUsage < MinimumUtilization)
    {
        fprintf(stderr,
            "O65 linker: You are trying to write a SMC file, but your %u-kilobyte ROM does\n"
            "            not seem to define the header stuff at address $%X.\n"
            "            (%u bytes found in that region.)\n"
            "            This ROM will probably not function properly.\n",
            (filesize+1023)/1024,
            HeaderBegin+0xB0, HeaderUsage);
    }
    
    unsigned sizebyte = ROMdata[HeaderBegin + 0xD7];
    if(rom_obj.GetUtilization(HeaderBegin + 0xD7, 1) == 0)
    {
        fseek(stream, HeaderBegin+0xD7, SEEK_SET);
        fputc(RomSizeSmallPow, stream);
        sizebyte = RomSizeSmallPow;
        
        fprintf(stderr, "O65 linker: Patching in the ROM size as %u kB (%u bytes, from %u)\n",
            1 << RomSizeSmallPow,
            1 << RomSize2pow,
            filesize);
    }
    else if(sizebyte != RomSizeSmallPow)
    {
        unsigned wanted_size = 1 << (10 + sizebyte);
        fprintf(stderr, "O65 linker: Your code tells that the ROM should be %u bytes in size. It is %u bytes.\n",
            wanted_size,
            filesize);
        if(wanted_size > filesize)
        {
            fprintf(stderr, "            Fixing this by extending the ROM size.\n");
            RomSize = wanted_size;
            ftruncate(fileno(stream), wanted_size);
            goto Do_Over;
        }
        else
        {
            fprintf(stderr, "            Fixing this by changing the size byte.\n");
            fseek(stream, HeaderBegin+0xD7, SEEK_SET);
            fputc(RomSizeSmallPow, stream);
            sizebyte = RomSizeSmallPow;
        }
    }
    
    unsigned sum1 = sizebyte + 0x00 + 0x00 + 0xFF + 0xFF;
    
    for(unsigned a=0; a<Pow2SizeDown; ++a)
    {
        /* Ignore the checksum region in checksum calculation,
         * because it might be incorrect.
         * Ignore also the sizebyte, because we changed it.
         */
        if(a >= HeaderBegin + 0xDC
        && a <  HeaderBegin + 0xE0) continue;
        if(a == HeaderBegin + 0xD7) continue;
        
        sum1 += ROMdata[a];
    }
    if(Pow2SizeDown < CalculatedSize)
    {
        unsigned Remainder = CalculatedSize - Pow2SizeDown;
        unsigned MirrorCount = Pow2SizeDown / Remainder;
        unsigned sum2 = 0;
        for(unsigned a=0; a<Remainder; ++a) sum2 += ROMdata[Pow2SizeDown + a];
        sum1 += sum2 * MirrorCount;
    }
    
    fprintf(stderr, "O65 linker: Writing checksum (do=%u,cc=%u, sum1=$%04X, sum2=$%04X)\n",
        Pow2SizeDown,CalculatedSize, sum1&0xFFFF, (sum1^0xFFFF)&0xFFFF);
    WriteCheckSumPair(stream, HeaderBegin, sum1);
}

static void Import(O65linker& linker, Object& obj, SegmentSelection seg)
{
    std::vector<unsigned> o65addrs = linker.GetAddrList(seg);
    for(unsigned a=0; a<o65addrs.size(); ++a)
    {
        const vector<unsigned char>& code = linker.GetSeg(seg, a);
        if(code.empty()) continue;
         
        char Buf[64];
        sprintf(Buf, "object_%u_%s", a+1, GetSegmentName(seg).c_str());
        
        obj.DefineLabel(Buf, o65addrs[a]);
        
        obj.SetPos(o65addrs[a]);
        obj.AddLump(code);
    }
}

static void WriteOut(O65linker& linker, std::FILE* stream)
{
    Object obj;
    
    obj.StartScope();
     obj.SelectTEXT(); Import(linker, obj, CODE);
     obj.SelectDATA(); Import(linker, obj, DATA);
     obj.SelectZERO(); Import(linker, obj, ZERO);
     obj.SelectBSS(); Import(linker, obj, BSS);
     obj.SelectTEXT();
    obj.EndScope();
    
    switch(format)   
    {
        case IPSformat:
            obj.WriteIPS(stream);
            break;
        case O65format:
            obj.WriteO65(stream);
            break;
        case RAWformat:
            obj.WriteRAW(stream);
            break;
        case SMCformat:
            obj.WriteRAW(stream, RomSize, 0);
            obj.SelectTEXT();
            FixupSMC(obj, stream);
            break;
    }
    obj.Dump();
}

int main(int argc, char** argv)
{
    std::vector<std::string> files;

    std::FILE *output = NULL;
    std::string outfn;

    for(;;)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"help",     0,0,'h'},
            {"version",  0,0,'V'},
            {"output",   0,0,'o'},
            {"outformat", 0,0,'f'},
            {"romsize",  0,0,'s'},
            {"romtype", 0,0,'t'},
            {"freespacemap",0,0,'m'},
            {0,0,0,0}
        };
        int c = getopt_long(argc,argv, "hVo:f:s:t:m:", long_options, &option_index);
        if(c==-1) break;
        switch(c)
        {
            case 'V': // version
            {
                printf(
                    "%s %s\n"
                    "Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "This is free software; see the source for copying conditions. There is NO\n" 
                    "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
                    argv[0], VERSION
                      );
                return 0;
            }
            
            case 'h':
            {
                std::printf(   
                    "O65 linker (tuned for SNES)\n"
                    "Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\nUsage: %s [<option> [<...>]] <file> [<...>]\n"
                    "\nLinks O65 and IPS files together and produces a file.\n"
                    "\nOptions:\n"
                    " --help, -h            This help\n"
                    " --version, -V         Displays version information\n"
                    " -f, --outformat <fmt> Select output format: ips,raw,o65,smc (default: ips)\n"
                    " -o <file>             Places the output into <file>\n"
                    " -s <size>             Desired size of the ROM (must be a power of 2, and >= 1024)\n"
                    "\nNo warranty whatsoever.\n",
                    argv[0]);
                return 0;
            }

            case 'o':
            {
                outfn = optarg;
                if(outfn != "-")
                {
                    if(output)
                    {
                        std::fclose(output);
                    }
                    output = std::fopen(outfn.c_str(), "wb");
                    if(!output)
                    {
                        std::perror(outfn.c_str());
                        goto ErrorExit;
                    }
                }
                break;
            }
            case 'f':
            {
                SetOutputFormat(optarg);
                break;
            }
            case 's':
            {
                unsigned outsize = strtol(optarg, 0, 10);
                unsigned pow = Calc2pow(outsize);
                unsigned powdsize = 1 << pow;
                
                if(outsize != powdsize || pow < 10)
                {
                    fprintf(stderr, "Warning: The given ROMsize (%u, 0x%X) is not a power of 2, or is smaller than 1024.\n"
                        "         Using %u (0x%X) instead.\n",
                        outsize, outsize,
                        powdsize, powdsize
                      );
                }
                RomSize = powdsize;
                break;
            }
            case 't':
                address_type = atoi(optarg);
                break;
            case 'm':
                freespacefile = new char[strlen(optarg)];
                strcpy(freespacefile, optarg);
                break;
        }
    }

    while(optind < argc)
        files.push_back(argv[optind++]);

    if(files.empty())
    {
        fprintf(stderr, "Error: Link what? See %s --help\n", argv[0]);
    ErrorExit:
        if(output)
        {
            fclose(output);
            remove(outfn.c_str());
        }
        return -1;
    }
    
    
    O65linker linker;
    
    for(unsigned a=0; a<files.size(); ++a)
    {
        char Buf[5];
        FILE *fp = fopen(files[a].c_str(), "rb");
        if(!fp)
        {
            perror(files[a].c_str());
            continue;
        }
        
        fread(Buf, 1, 5, fp);
        if(!strncmp(Buf, "PATCH", 5))
        {
            linker.LoadIPSfile(fp, files[a]);
        }
        else
        {
            O65 tmp;
            tmp.Load(fp);
            
            const vector<pair<unsigned char, string> >&
                customheaders = tmp.GetCustomHeaders();
            
            LinkageWish Linkage;
            
            for(unsigned b=0; b<customheaders.size(); ++b)
            {
                unsigned char type = customheaders[b].first;
                const string& data = customheaders[b].second;
                switch(type)
                {
                    case 10: // linkage type
                    {
                        unsigned param = 0;
                        if(data.size() >= 5)
                        {
                            param = (data[1] & 0xFF) 
                                  | ((data[2] & 0xFF) << 8)
                                  | ((data[3] & 0xFF) << 16)
                                  | ((data[4] & 0xFF) << 24);
                        }
                        switch(data[0])
                        {
                            case 0:
                                Linkage = LinkageWish();
                                break;
                            case 1:
                                Linkage.SetLinkageGroup(param);
                                fprintf(stderr, "%s will be linked in group %u\n",
                                    files[a].c_str(), param);
                                break;
                            case 2:
                                Linkage.SetLinkagePage(param);
                                fprintf(stderr, "%s will be linked to page $%02X\n",
                                    files[a].c_str(), param);
                                break;
                        }
                        break;
                    }
                    case 0: // filename
                    case 1: // operating system header
                    case 2: // assembler name
                    case 3: // author
                    case 4: // creation date
                        break;
                }
            }
            
            linker.AddObject(tmp, files[a], Linkage);
        }
        fclose(fp);
    }
    
    freespacemap freespace_code;
    LoadFreespaceSpecs(freespace_code);
    /* Organize the code blobs */    
    freespace_code.OrganizeO65linker(linker, CODE);
    
    /* ZERO, DATA, BSS all refer to the RAM. */
    freespacemap freespace_data;

    /* First link the zeropage. It may only use 8-bit addresses. */
    freespace_data.Add(0x7E0000, 0x100);
    freespace_data.OrganizeO65linker(linker, ZERO);

    /* Then link data and bss. They are interchangeable.
     * If 8-bit addresses remained free from the zeropage segment,
     * they may be used for data addresses.
     */
    freespace_data.Add(0x7E0100, GetPageSize() - 0x100);
    freespace_data.Add(0x7F0000, GetPageSize());
    freespace_data.OrganizeO65linker(linker, DATA);
    freespace_data.OrganizeO65linker(linker, BSS);
    
    linker.Link();
    
    WriteOut(linker, output ? output : stdout);
    if(output) fclose(output);
    
    return 0;
}
