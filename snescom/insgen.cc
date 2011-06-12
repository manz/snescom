#include <cstdio>
#include <cctype>
#include <string>
#include <map>

struct ins
{
    char token[4];
    int opcodes[25];
};

int main(void)
{
    static const char data[] = 
 "DKDTGGGMABYAOOORELJUGHHNAQYAOPPSOKRTGGGMABYAOOORELJUHHHNAQYAPPPSA"
 "KDTZGGMABYAOOORELJUZHHNAQAARPPSAKFTGGGMABYAVOORELJUHHHNAQAAXPPSEKFTGGGMABAA"
 "OOORELJUHHINAQAAOPPSCKCTGGGMABAAOOORELJUHHINAQAAPPQSCKDTGGGMABAAOOORELJJBHH"
 "NAQAAWPPSCKDTGGGMABAAOOORELJUOHHNAQAAXPPSADCANDASLBCCBCSBEQBITBMIBNEBPLBRAB"
 "RKBRLBVCBVSCLCCLDCLICLVCMPCOPCPXCPYDB DECDEXDEYEORINCINXINYJMLJMPJSLJSRLDAL"   
 "DXLDYLSRMVNMVPNOPORAPEAPEIPERPHAPHBPHDPHKPHPPHXPHYPLAPLBPLDPLPPLXPLYREPROLR"
 "ORRTIRTLRTSSBCSECSEDSEISEPSTASTPSTXSTYSTZTAXTAYTCDTCSTDCTRBTSBTSCTSXTXATXST"
 "XYTYATYXWAIXBAXCE.M7MtM%MUM%StM%M,MMMsM%M2M?qsM%ME$D$)$_$[$_Z)$_$*$$$)$_$e$"
 ";u)$_$a>:>K>I>Q>ITC>I>0>>>J>I>4>WpC>I>c#P#m#`#X#`bC#`#1###m#`#g#]rC#`#-i/il"
 "iki=)wRliki&iiilikizixymimiHFGFHFGFoFnYHFGF'FFFHFGF5Fv{HFGF96^696;6A6<|96;6"
 "+666O6;636VjB6;68dhd8d?d@dL}8d?d(dddNd?dfd\\~Ed?d";
 
    // ins -> type -> opcode
    typedef std::map<unsigned, unsigned char> map2;
    typedef std::map<std::string, map2> map1;
    
    map1 insdata;
    
    for(unsigned a=0; a<256; ++a)
    {
        static const int remap[] =
        {
            /* 0-4 */    0,1,2,3,4,
            /* 5-9 */    5,6,7,8,9,
            /* 10-14 */  10,11,12,13,14,
            /* 15-19 */  15,16,17,18,19,
            /* 20-24 */  20,21,22,23,0,
            /* 25-29 */  24,14
        };
    
        char Ins[4];
        sprintf(Ins, "%.3s", data + 256 + 3 * (data[256+3*92+a] - 35));
        unsigned type = data[a] - 'A';
        for(unsigned b=0; b<3; ++b)
            Ins[b] = std::tolower(Ins[b]);
        
        type = remap[type];
        
        insdata[Ins][type] = a;
    }
    
    std::printf(
        "static const struct ins\n"
        "{\n"
        "    char token[4];\n"
        "    char opcodes[25*3];\n"
        "} ins[] =\n"
        "{\n");
    
    std::printf("//ins    ");
    for(unsigned a=0; a<25; ++a)std::printf("%2d ", a);
    std::printf("\n");
    
    for(map1::const_iterator i = insdata.begin(); i != insdata.end(); ++i)
    {
        std::printf("  { \"%s\", \"", i->first.c_str());
        
        for(unsigned a=0; a<25; ++a)
        {
            map2::const_iterator j = i->second.find(a);
            if(j == i->second.end())
                std::printf("--");
            else
                std::printf("%02X", j->second);
            if(a<24)std::printf("'");
        }
        std::printf("\"},\n");
    }
    std::printf("};\n");
}
