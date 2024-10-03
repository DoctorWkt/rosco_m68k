	section .text

; CH375 I/O addresses
CHDATARD  equ	$FFF001
CHDATAWR  equ	$FFF001
CHCMDWR   equ	$FFF003

DUART_SRA equ   $F00003
DUART_TBA equ   $F00007

; Address of the IRQ5 vector
; the uninitialised interrupt vector
; and the spurious interrupt vector
IRQ5_VECTOR equ $74
UINIT_VECTOR equ $3C
SPUR_VECTOR equ $60

; Location where the interrupt handler
; stores the CH375 status byte
CH375_STATUS  equ $500

; CH375 Commands
CMD_GET_STATUS  equ  $22

; This comes from xosera_m68k_api.c
cpu_delay::
	move.l 4(A7),D0
	lsl.l  #8,D0
	add.l  D0,D0
L1:	subq.l #1,D0
	tst.l  D0
	bne.s  L1
	rts

; Print 'x' to UART A.
pcharx::
.BUFF_WAIT:
	btst.b  #3,DUART_SRA
	beq.s   .BUFF_WAIT
	move.b  #120,DUART_TBA
	rts

; This is the interrupt handler for the CH375.
; Send a CMD_GET_STATUS to the device,
; read a byte of data and save it in the
; CH375_STATUS location.
;
; Also print 'x' on the terminal so we know if
; we even saw the interrupt.
irq5_handler::
	move.b #CMD_GET_STATUS,CHCMDWR
	move.b CHDATARD,CH375_STATUS
	rte

; Install the IRQ handler and put a
; dummy value in the CH375_STATUS byte.
irq5_install::
	move.l #irq5_handler,IRQ5_VECTOR
	move.b #$FF,CH375_STATUS
	and.w  #$F0FF,SR                ; Enable all interrupts
	rts

; Write the given command to the CH375.
; Clear the CH375_STATUS beforehand by
; putting a dummy value there.
send_ch375_cmd::
	move.b #$FF,CH375_STATUS
	move.b 7(A7),D0
	move.b D0,CHCMDWR
	rts

; Write the given data to the CH375.
send_ch375_data::
	move.b 7(A7),D0
	move.b D0,CHDATAWR
	rts

; Read data from the CH375
read_ch375_data::
	move.b CHDATARD,D0
	rts

; Get the CH375 status from the
; CH375_STATUS memory location
get_ch375_status::
	move.b CH375_STATUS,D0
	rts
