#ifndef bqt65asmObjectHH
#define bqt65asmObjectHH

#include <string>
#include <unistd.h>
#include "o65linker.hh"

class Object
{
public:
    Object();
    ~Object();
    
    // Clears everything else but flip-positions
    void ClearMost();
    
    void StartScope();
    void EndScope();
    
    void GenerateByte(unsigned char byte);
    void AddLump(const std::vector<unsigned char>& lump);

    void AddExtern(char prefix, const std::string& ref, long value);
    
    void DefineLabel(const std::string& label);
    void DefineLabel(const std::string& label, unsigned value);
    void UndefineLabel(const std::string& label);

    void SetPos(unsigned newpos);
    unsigned GetPos() const;
    
    void SelectTEXT() { CurSegment = CODE; }
    void SelectDATA() { CurSegment = DATA; }
    void SelectZERO() { CurSegment = ZERO; }
    void SelectBSS() { CurSegment = BSS; }
    
    unsigned GetSegmentBase() const;
    unsigned GetSegmentSize() const;
    std::vector<unsigned char> GetContent() const;
    std::vector<unsigned char> GetContent(unsigned begin, unsigned size) const;

    unsigned GetUtilization(unsigned begin, unsigned size) const;
    
    void CloseSegments();
    
    void Dump();
    
    void WriteO65(std::FILE* fp);
    void WriteIPS(std::FILE* fp);
    void WriteRAW(std::FILE* fp, unsigned size=0, unsigned offset=0);
    
    // If a REL8 should be flipped at this position
    bool ShouldFlipHere() const;
    
    bool NeedsFlipping() const;

    bool FindLabel(const std::string& s) const;

    bool FindLabel(const std::string& name,
                   SegmentSelection& seg, unsigned& result) const;
    
    bool FindLabel(const std::string& name, unsigned level,
                   SegmentSelection& seg, unsigned& result) const;
    
public:
    class Segment;

private:
    // private variables
    
    Segment *code, *data, *zero, *bss;
    unsigned CurScope;
    SegmentSelection CurSegment;

public:
    LinkageWish Linkage;

private:
    // private methods
    
    Segment& GetSeg();
    const Segment& GetSeg() const;
    
    void DumpLabels() const;
    void DumpExterns() const;
    void DumpFixups() const;
    
private:
    // no copying
    Object(const Object&);
    void operator=(const Object&);
};

extern bool fix_jumps;

#endif
