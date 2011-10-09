#ifndef bqtRomAddrHH
#define bqtRomAddrHH

unsigned char ROM2SNESpage(unsigned char page);
unsigned char SNES2ROMpage(unsigned char page);
unsigned int ROM2SNESaddr(unsigned int addr);
unsigned int SNES2ROMaddr(unsigned int addr);
unsigned int ROM2SNESaddr(unsigned int addr, int mode);

bool IsSNESbased(unsigned int addr);

/* Returns true if the given ROM is probably a HiROM */
bool GuessROMtype(const unsigned char* ROM, unsigned ROMsize);

/* Guesses the position of the ROM header in the ROM.
 * It is 0xFF00 or 0x7F00.
 */
unsigned GuessROMheaderOffset(const unsigned char* ROM, unsigned ROMsize);

unsigned GetPageSize()
#ifdef __GNUC__
 __attribute__((pure))
#endif
 ;

#endif
