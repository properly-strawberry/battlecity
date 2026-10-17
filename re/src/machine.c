/* Machine state and entry points of the reconstruction. */
#include "machine.h"
#include "mech/rt.h"

uint8_t RAM[0x800];
uint8_t A, X, Y, S;
uint8_t fC, fZ, fN, fV, fI, fD;

void sub_C070(void);
void sub_D400(void);

/* RESET vector $FFFC -> $C070. The CPU comes out of reset with S = $FD
 * and I set; the game then sets S = $7F itself. */
void bc_reset(void) {
    S = 0xFD; fI = 1;
    sub_C070();
}

/* NMI vector $FFFA -> $D400. In the original the NMI always interrupts the
 * main thread inside the wait loop at $D8F8 (measured: 120k frames of play),
 * so the interrupted PC pushed here is $D8F8. */
void bc_nmi(void) {
    uint8_t s0 = S;
    PUSH(0xD8); PUSH(0xF8);
    PUSH(GETP() & ~0x10);
    fI = 1;
    sub_D400();
    S = s0;   /* balanced for both the mechanical (RTI) and a lifted handler */
}
