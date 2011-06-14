#include <cctype>
#include <string>
#include <utility>
#include <vector>
#include <list>
#include <map>

#include "expr.hh"
#include "parse.hh"
#include "assemble.hh"
#include "object.hh"
#include "insdata.hh"
#include "precompile.hh"

bool A_16bit = true;
bool X_16bit = true;

int address_type = 3;

static std::map<unsigned, std::string> PrevBranchLabel; // What "-" means (for each length of "-")
static std::map<unsigned, std::string> NextBranchLabel; // What "+" means (for each length of "+")

#define SHOW_CHOICES   0
#define SHOW_POSSIBLES 0

namespace
{
    struct OpcodeChoice
    {
        typedef std::pair<unsigned, struct ins_parameter> paramtype;
        std::vector<paramtype> parameters;
        bool is_certain;
        
    public:
        OpcodeChoice(): parameters(), is_certain(false) { }
        void FlipREL8();
    };

    void OpcodeChoice::FlipREL8()
    {
        // Must have:
        //   - Two parameters
        //   - First param must be 1-byte const
        //   - Second param must be 1-byte and of type REL8
        
        std::vector<std::string> errors;
        
        if(parameters.size() != 2)
        {
            char Buf[128];
            std::sprintf(Buf, "Parameter count (%u) is not 2", parameters.size());
            errors.push_back(Buf);
        }
        if(parameters[0].first != 1)
        {
            char Buf[128];
            std::sprintf(Buf, "Parameter 1 is not byte (size is %u bytes)", parameters[0].first);
            errors.push_back(Buf);
        }
        if(parameters[1].first != 1)
        {
            char Buf[128];
            std::sprintf(Buf, "Parameter 2 is not byte (size is %u bytes)", parameters[1].first);
            errors.push_back(Buf);
        }
        if(!parameters[0].second.exp->IsConst())
        {
            char Buf[128];
            std::sprintf(Buf, "Parameter 1 is not const");
            errors.push_back(Buf);
        }
        if(parameters[1].second.prefix != FORCE_REL8)
        {
            char Buf[128];
            std::sprintf(Buf, "Parameter 2 is not REL8");
            errors.push_back(Buf);
        }
        
        if(!errors.empty())
        {
            std::fprintf(stderr, "Error: REL8-fixing when\n");
            for(unsigned a=0; a<errors.size(); ++a)
                std::fprintf(stderr, "- %s\n", errors[a].c_str());
            std::fprintf(stderr, "- Opcode:");
            for(unsigned a=0; a<parameters.size(); ++a)
            {
                std::fprintf(stderr, " (%u)%s",
                    parameters[a].first,
                    parameters[a].second.Dump().c_str());
            }
            std::fprintf(stderr, "\n");
            return;
        }
        
        unsigned char opcode = parameters[0].second.exp->GetConst();
        
        if(opcode == 0x80)
            opcode = 0;     // No prefix-opcode
        else
        {
            // 10 30 bpl bmi
            // 50 70 bvc bvs
            // 90 B0 bcc bcs
            // D0 F0 bne beq
            opcode ^= 0x20; // This flips the polarity
        }
        
        std::vector<paramtype> newparams;
        if(opcode != 0)
        {
            // Insert a reverse-jump-by.
            newparams.push_back(paramtype(1, opcode));
            newparams.push_back(paramtype(1, 0x03));
        }
        newparams.push_back(paramtype(1, 0x82)); // Insert BRL
        
        ins_parameter newparam;
        newparam.prefix = FORCE_REL16;
        newparam.exp    = parameters[1].second.exp;
        newparams.push_back(paramtype(2, newparam));
        
        // FIXME: unallocate param1 here
        
        // Replace with the new instruction.
        parameters = newparams;
    }

    typedef std::vector<OpcodeChoice> ChoiceList;
    
    std::list<std::string> DefinedBranchLabels;
    
    void CreateNewPrevBranch(unsigned length)
    {
        static unsigned BranchNumber = 0;
        char Buf[128];
        std::sprintf(Buf, "$PrevBranch%u$%u", length,++BranchNumber);
        
        DefinedBranchLabels.push_back(PrevBranchLabel[length] = Buf);
    }
    void CreateNewNextBranch(unsigned length)
    {
        static unsigned BranchNumber = 0;
        char Buf[128];
        std::sprintf(Buf, "$NextBranch%u$%u", length,++BranchNumber);
        
        DefinedBranchLabels.push_back(NextBranchLabel[length] = Buf);
    }
    const std::string CreateNopLabel()
    {
        static unsigned BranchNumber = 0;
        char Buf[128];
        std::sprintf(Buf, "$NopLabel$%u", ++BranchNumber);
        
        DefinedBranchLabels.push_back(Buf);
        return Buf;
    }
    
    unsigned ParseConst(ins_parameter& p, const Object& obj)
    {
        unsigned value = 0;
        
        std::set<std::string> labels;
        FindExprUsedLabels(&*p.exp, labels);
        
        for(std::set<std::string>::const_iterator
            i = labels.begin(); i != labels.end(); ++i)
        {
            const std::string& label = *i;

            SegmentSelection seg;

            if(obj.FindLabel(label, seg, value))
            {
                expression* e = p.exp.get();
                SubstituteExprLabel(e, label, value, false);
                boost::shared_ptr<expression> tmp(e);
                p.exp.swap(tmp);
            }
            else
            {
                fprintf(stderr,
                    "Error: Undefined label \"%s\" in expression - got \"%s\"\n",
                    label.c_str(), p.Dump().c_str());
            }
        }
        
        if(p.exp->IsConst())
            value = p.exp->GetConst();
        else
        {
            fprintf(stderr,
                "Error: Expression must be const - got \"%s\"\n",
                p.Dump().c_str());
        }
        
        return value;
    }
    
    void ParseIns(ParseData& data, Object& result)
    {
    MoreLabels:
        std::string tok;
        
        data.SkipSpace();
        
        if(IsDelimiter(data.PeekC()))
        {
            data.GetC();
            goto MoreLabels;
        }

        if(data.PeekC() == '+')
        {
            do {
                tok += data.GetC(); // defines global
            } while(data.PeekC() == '+');
            goto GotLabel;
        }
        else if(data.PeekC() == '-')
        {
            do {
                tok += data.GetC();
            } while(data.PeekC() == '-');
            goto GotLabel;
        }
        else
        {
            while(data.PeekC() == '&') tok += data.GetC();
        }
        
        if(data.PeekC() == '*')
        {
            tok += data.GetC();
            goto GotLabel;
        }
        
        // Label may not begin with ., but instruction may
        if(tok.empty() && data.PeekC() == '.')
            tok += data.GetC();
        
        for(bool first=true;; first=false)
        {
            char c = data.PeekC();
            if(isalpha(c) || c == '_'
            || (!first && isdigit(c))
            || (tok[0]=='.' && ispunct(c))
              )
                tok += data.GetC();
            else
            {
                break;
            }
        }

GotLabel:
        data.SkipSpace();
        if(!tok.empty() && tok[0] == '+') // It's a next-branch-label
        {
            unsigned length = tok.size();
            result.DefineLabel(NextBranchLabel[length]);
            CreateNewNextBranch(length);
            goto MoreLabels;
        }
        if(!tok.empty() && tok[0] == '-') // It's a prev-branch-label
        {
            unsigned length = tok.size();
            CreateNewPrevBranch(length);
            result.DefineLabel(PrevBranchLabel[length]);
            goto MoreLabels;
        }
        
        data.SkipSpace();
        
        std::vector<OpcodeChoice> choices;
        
        const struct ins *insdata = std::lower_bound(ins, ins+InsCount, tok);
        if(insdata == ins+InsCount || tok != insdata->token)
        {
            /* Other mnemonic */
            
            if (tok == ".lowrom") {
                address_type = 1;
            }
            else if (tok == ".lowrom2") {
                address_type = 2;
            }
            else if (tok == ".highrom") {
                address_type = 3;
            }
            else if (tok == ".incbin") {
                // TODO: read filename & open !
                fprintf(stderr, "(EE) %s\n", "incbin not supported yet");
               
            }
            else if(tok == ".byt")
            {
                OpcodeChoice choice;
                bool first=true, ok=true;
                for(;;)
                {
                    data.SkipSpace();
                    if(data.EOF()) break;
                    
                    if(first)
                        first=false;
                    else
                    {
                        if(data.PeekC() == ',') { data.GetC(); data.SkipSpace(); }
                    }

                    if(data.PeekC() == '"')
                    {
                        data.GetC();
                        while(!data.EOF())
                        {
                            char c = data.GetC();
                            if(c == '"') break;
                            if(c == '\\')
                            {
                                c = data.GetC();
                                if(c == 'n') c = '\n';
                                else if(c == 'r') c = '\r';
                            }
                            ins_parameter p(c);
                            choice.parameters.push_back(std::make_pair(1, p));
                        }
                        continue;
                    }
                    
                    ins_parameter p;
                    if(!ParseExpression(data, p) || !p.is_byte())
                    {
                        /* FIXME: syntax error */
                        ok = false;
                        break;
                    }
                    choice.parameters.push_back(std::make_pair(1, p));
                }
                if(ok)
                {
                    choice.is_certain = true;
                    choices.push_back(choice);
                }
            }
            else if(tok == ".word")
            {
                OpcodeChoice choice;
                bool first=true, ok=true;
                for(;;)
                {
                    data.SkipSpace();
                    if(data.EOF()) break;
                    
                    if(first)
                        first=false;
                    else
                    {
                        if(data.PeekC() == ',') { data.GetC(); data.SkipSpace(); }
                    }
                    
                    ins_parameter p;
                    if(!ParseExpression(data, p)
                    || p.is_word().is_false())
                    {
                        /* FIXME: syntax error */
                        std::fprintf(stderr, "Syntax error at '%s'\n",
                            data.GetRest().c_str());
                        ok = false;
                        break;
                    }
                    choice.parameters.push_back(std::make_pair(2, p));
                }
                if(ok)
                {
                    choice.is_certain = true;
                    choices.push_back(choice);
                }
            }
            else if(tok == ".long")
            {
                OpcodeChoice choice;
                bool first=true, ok=true;
                for(;;)
                {
                    data.SkipSpace();
                    if(data.EOF()) break;
                    
                    if(first)
                        first=false;
                    else
                    {
                        if(data.PeekC() == ',') { data.GetC(); data.SkipSpace(); }
                    }
                    
                    ins_parameter p;
                    if(!ParseExpression(data, p)
                    || p.is_long().is_false())
                    {
                        /* FIXME: syntax error */
                        std::fprintf(stderr, "Syntax error at '%s'\n",
                            data.GetRest().c_str());
                        ok = false;
                        break;
                    }
                    choice.parameters.push_back(std::make_pair(3, p));
                }
                if(ok)
                {
                    choice.is_certain = true;
                    choices.push_back(choice);
                }
            }
            else if(!tok.empty() && tok[0] != '.')
            {
                // Labels may not begin with '.'
                
                if(data.PeekC() == '=')
                {
                    // Define label as something
                    
                    data.GetC(); data.SkipSpace();
                    
                    ins_parameter p;
                    if(!ParseExpression(data, p))
                    {
                        fprintf(stderr, "Expected expression: %s\n", data.GetRest().c_str());
                    }
                    
                    unsigned value = ParseConst(p, result);
                    
                    p.exp.reset();
                    
                    if(tok == "*")
                    {
                        result.SetPos(SNES2ROMaddr(value));
                    }
                    else
                    {
                        result.DefineLabel(tok, value);
                    }
                }
                else
                {
                    if(tok == "*")
                    {
                        std::fprintf(stderr,
                            "Cannot define label '*'. Perhaps you meant '*= <value>'?\n"
                                    );
                    }
                    result.DefineLabel(tok);
                }
                goto MoreLabels;
            }
            else if(!data.EOF())
            {
                std::fprintf(stderr,
                    "Error: What is '%s' - previous token: '%s'?\n",
                        data.GetRest().c_str(),
                        tok.c_str());
            }
        }
        else
        {
            /* Found mnemonic */
            
            bool something_ok = false;
            for(unsigned addrmode=0; insdata->opcodes[addrmode*3]; ++addrmode)
            {
                const std::string op(insdata->opcodes+addrmode*3, 2);
                if(op != "--")
                {
                    ins_parameter p1, p2;
                    
                    const ParseData::StateType state = data.SaveState();
                    
                    tristate valid = ParseAddrMode(data, addrmode, p1, p2);
                    if(!valid.is_false())
                    {
                        something_ok = true;
                        
                        if(op == "sb") result.StartScope();
                        else if(op == "eb") result.EndScope();
                        else if(op == "as") A_16bit = false;
                        else if(op == "al") A_16bit = true;
                        else if(op == "xs") X_16bit = false;
                        else if(op == "xl") X_16bit = true;
                        else if(op == "gt") result.SelectTEXT();
                        else if(op == "gd") result.SelectDATA();
                        else if(op == "gz") result.SelectZERO();
                        else if(op == "gb") result.SelectBSS();
                        else if(op == "li")
                        {
                            switch(addrmode)
                            {
                                case 26: // .link group 1
                                {
                                    result.Linkage.SetLinkageGroup(ParseConst(p1, result));
                                    p1.exp.reset();
                                    break;
                                }
                                case 27: // .link page $FF
                                {
                                    result.Linkage.SetLinkagePage(ParseConst(p1, result));
                                    p1.exp.reset();
                                    break;
                                }
                                default:
                                    // shouldn't happen
                                    break;
                            }
                        }
                        else if(op == "np")
                        {
                            switch(addrmode)
                            {
                                case 28: // word imm
                                {
                                    unsigned imm16 = ParseConst(p1, result);
                                    
                                    OpcodeChoice choice;
                                    
                                    if(imm16 > 127+3)
                                    {
                                        std::string NopLabel = CreateNopLabel();
                                        result.DefineLabel(NopLabel, result.GetPos()+imm16);
                                        
                                        // jmp
                                        choice.parameters.push_back(std::make_pair(1, 0x82)); // BRL
                                        
                                        expression* e = new expr_label(NopLabel);
                                        boost::shared_ptr<expression> tmp(e);    
                                        
                                        p1.prefix = FORCE_REL16;
                                        p1.exp.swap(tmp);
                                        choice.parameters.push_back(std::make_pair(2, p1));
                                        
                                        imm16 -= 3;
                                    }
                                    else if(imm16 > 2)
                                    {
                                        std::string NopLabel = CreateNopLabel();
                                        result.DefineLabel(NopLabel, result.GetPos()+imm16);
                                        
                                        // jmp
                                        choice.parameters.push_back(std::make_pair(1, 0x80)); // BRA
                                        
                                        expression* e = new expr_label(NopLabel);
                                        boost::shared_ptr<expression> tmp(e);    
                                        
                                        p1.prefix = FORCE_REL8;
                                        p1.exp.swap(tmp);
                                        choice.parameters.push_back(std::make_pair(1, p1));
                                        
                                        imm16 -= 2;
                                    }
                                    
                                    // Fill the rest with nops
                                    for(unsigned n=0; n<imm16; ++n)
                                        choice.parameters.push_back(std::make_pair(1, 0xEA));
                                    
                                    choice.is_certain = valid.is_true();
                                    choices.push_back(choice);
                                    break;
                                }
                                default:
                                    // shouldn't happen
                                    break;
                            }
                        }
                        else
                        {
                            // Opcode in hex.
                            unsigned char opcode = std::strtol(op.c_str(), NULL, 16);
                            
                            OpcodeChoice choice;
                            unsigned op1size = GetOperand1Size(addrmode);
                            unsigned op2size = GetOperand2Size(addrmode);
                            
                            if(AddrModes[addrmode].p1 == AddrMode::tRel8)
                                p1.prefix = FORCE_REL8;
                            if(AddrModes[addrmode].p1 == AddrMode::tRel16)
                                p1.prefix = FORCE_REL16;
                            
                            choice.parameters.push_back(std::make_pair(1, opcode));
                            if(op1size)choice.parameters.push_back(std::make_pair(op1size, p1));
                            if(op2size)choice.parameters.push_back(std::make_pair(op2size, p2));

                            choice.is_certain = valid.is_true();
                            choices.push_back(choice);
                        }
#if SHOW_POSSIBLES
                        std::fprintf(stderr, "- %s mode %u (%s) (%u bytes)\n",
                            valid.is_true() ? "Is" : "Could be",
                            addrmode, op.c_str(),
                            GetOperandSize(addrmode)
                                    );
                        if(p1.exp)
                            std::fprintf(stderr, "  - p1=\"%s\"\n", p1.Dump().c_str());
                        if(p2.exp)
                            std::fprintf(stderr, "  - p2=\"%s\"\n", p2.Dump().c_str());
#endif
                    }
                    
                    data.LoadState(state);
                }
                if(!insdata->opcodes[addrmode*3+2]) break;
            }

            if(!something_ok)
            {
                std::fprintf(stderr,
                    "Error: '%s' is invalid parameter for '%s' in current context (%u choices).\n",
                        data.GetRest().c_str(),
                        tok.c_str(),
                        choices.size());
                return;
            }
        }

        if(choices.empty())
        {
            //std::fprintf(stderr, "? Confused before '%s'\n", data.GetRest().c_str());
            return;
        }

        /* Try to pick one the smallest of the certain choices */
        unsigned smallestsize = 0, smallestnum = 0;
        bool found=false;
        for(unsigned a=0; a<choices.size(); ++a)
        {
            const OpcodeChoice& c = choices[a];
            if(c.is_certain)
            {
                unsigned size = 0;
                for(unsigned b=0; b<c.parameters.size(); ++b)
                    size += c.parameters[b].first;
                if(size < smallestsize || !found)
                {
                    smallestsize = size;
                    smallestnum = a;
                    found = true;
                }
            }
        }
        if(!found)
        {
            /* If there were no certain choices, try to pick one of the uncertain ones. */
            for(unsigned a=0; a<choices.size(); ++a)
            {
                const OpcodeChoice& c = choices[a];
                
                unsigned sizediff = 0;
                for(unsigned b=0; b<c.parameters.size(); ++b)
                {
                    int diff = 2 - (int)c.parameters[b].first;
                    if(diff < 0)diff = -diff;
                    sizediff += diff;
                }
                if(sizediff < smallestsize || !found)
                {
                    smallestsize = sizediff;
                    smallestnum = a;
                    found = true;
                }
            }
        }
        if(!found)
        {
            std::fprintf(stderr, "Internal error\n");
        }
        
        OpcodeChoice& c = choices[smallestnum];
        
        if(result.ShouldFlipHere())
        {
            //std::fprintf(stderr, "Flipping...\n");
            c.FlipREL8();
        }
        
#if SHOW_CHOICES
        std::fprintf(stderr, "Choice %u:", smallestnum);
#endif
        for(unsigned b=0; b<c.parameters.size(); ++b)
        {
            long value = 0;
            std::string ref;
            
            unsigned size = c.parameters[b].first;
            const ins_parameter& param = c.parameters[b].second;
            
            const expression* e = param.exp.get();
            
            if(e->IsConst())
            {
                value = e->GetConst();
            }
            else if(const expr_label* l = dynamic_cast<const expr_label* > (e))
            {
                ref = l->GetName();
            }
            else if(const sum_group* s = dynamic_cast<const sum_group* > (e))
            {
                /* constant should always be last in a sum group. */
                
                /* sum_group always has at least 2 elements.
                 * If it has 0, it's converted to expr_number.
                 * if it has 1, it's converter into the element itself (or expr_negate).
                 */
                
                sum_group::list_t::const_iterator first = s->contents.begin();
                sum_group::list_t::const_iterator last = s->contents.end(); --last;
                
                const char *error = NULL;
                if(s->contents.size() != 2)
                {
                    error = "must have 2 elements";
                }
                else if(!last->first->IsConst()) // If 2nd isn't const
                {
                    error = "2nd elem isn't const";
                }
                else if(first->second)          // If 1st isn't positive
                {
                    error = "1st elem must not be negative";
                }
                else if(!dynamic_cast<const expr_label *> (first->first))
                {
                    error = "1st elem must be a label";
                }
                if(error)
                {
                    /* Invalid pointer arithmetic */
                    std::fprintf(stderr, "Invalid pointer arithmetic (%s): '%s'\n",
                        error,
                        e->Dump().c_str());
                    continue;
                }
               
                ref = (dynamic_cast<const expr_label *> (first->first))->GetName();
                value = last->first->GetConst();
            }
            else
            {
                fprintf(stderr, "Invalid parameter (not a label/const/label+const): '%s'\n",
                    e->Dump().c_str());
                continue;
            }
            
            char prefix = param.prefix;
            if(!prefix)
            {
                // Pick a prefix that represents what we're actually doing here
                switch(size)
                {
                    case 1: prefix = FORCE_LOBYTE; break;
                    case 2: prefix = FORCE_ABSWORD; break;
                    case 3: prefix = FORCE_LONG; break;
                    default:
                        std::fprintf(stderr, "Internal error - unknown size: %u\n",
                            size);
                }
            }
            
            if(!ref.empty())
            {
                result.AddExtern(prefix, ref, value);
                value = 0;
            }
            else if(prefix == FORCE_REL8 || prefix == FORCE_REL16)
            {
                std::fprintf(stderr, "Error: Relative target must not be a constant\n");
                // FIXME: It isn't so bad...
            }

            switch(prefix)
            {
                case FORCE_SEGBYTE:
                    result.GenerateByte((value >> 16) & 0xFF);
                    break;
                case FORCE_LONG:
                    result.GenerateByte(value & 0xFF);
                    value >>= 8;
                    //passthru
                case FORCE_ABSWORD:
                    result.GenerateByte(value & 0xFF);
                    //passthru
                case FORCE_HIBYTE:
                    value >>= 8;
                    //passthru
                case FORCE_LOBYTE:
                    result.GenerateByte(value & 0xFF);
                    break;
                
                case FORCE_REL16:
                    result.GenerateByte(0x00);
                    //passthru
                case FORCE_REL8:
                    result.GenerateByte(0x00);
                    break;
            }
            
#if SHOW_CHOICES
            std::fprintf(stderr, " %s(%u)", param.Dump().c_str(), size);
#endif
        }
#if SHOW_CHOICES
        if(c.is_certain)
            std::fprintf(stderr, " (certain)");
        std::fprintf(stderr, "\n");
#endif
        
        // *FIXME* choices not properly deallocated
    }

    void ParseLine(Object& result, const std::string& s)
    {
        // Break into statements, assemble each by each
        for(unsigned a=0; a<s.size(); )
        {
            unsigned b=a;
            bool quote = false;
            while(b < s.size())
            {
                if(quote && s[b] == '\\' && (b+1) < s.size()) ++b;
                if(s[b] == '"') quote = !quote;
                if(!quote && IsDelimiter(s[b]))break;
                ++b;
            }
            
            if(b > a)
            {
                const std::string tmp = s.substr(a, b-a);
                //std::fprintf(stderr, "Parsing '%s'\n", tmp.c_str());
                ParseData data(tmp);
                ParseIns(data, result);
            }
            a = b+1;
        }
    }
}

const std::string& GetPrevBranchLabel(unsigned length)
{
    std::string& l = PrevBranchLabel[length];
    if(l.empty()) CreateNewPrevBranch(length);
    return l;
}

const std::string& GetNextBranchLabel(unsigned length)
{
    std::string& l = NextBranchLabel[length];
    if(l.empty()) CreateNewNextBranch(length);
    return l;
}

void AssemblePrecompiled(std::FILE *fp, Object& obj)
{
    if(!fp)
    {
        return;
    }
    
    obj.StartScope();
    obj.SelectTEXT();
    
    for(;;)
    {
        char Buf[16384];
        if(!std::fgets(Buf, sizeof Buf, fp)) break;
        
        if(Buf[0] == '#')
        {
            // Probably something generated by gcc
            continue;
        }
        ParseLine(obj, Buf);
    }

    obj.EndScope();

    for(std::list<std::string>::const_iterator
        i = DefinedBranchLabels.begin();
        i != DefinedBranchLabels.end();
        ++i)
    {
        obj.UndefineLabel(*i);
    }
    DefinedBranchLabels.clear();
}
