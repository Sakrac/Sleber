const VRAM = $4000
const VRAM_SET = 2 ; 0:$c000, 1:$8000, 2:$4000, 3:$0000
const Screen = $3c00
const Chars = $6000
const ColorSrc = Screen - 1000
const ColorDst = $d800

org ColorSrc - 100

    sei
    {
        lda $d011
        bmi !
    }
    {
        lda $d011
        bpl !
    }
	; set up screen mode
	lda $dd00	 ; vic bank = $4000
	and #$fc
	ora #VRAM_SET
	sta $dd00
Value_d011 = *+1
	lda #$1b
	sta $d011
Value_d016 = *+1
	lda #$c8
	sta $d016
Value_d020 = *+1
    lda #0
    sta $d020
    lda #0
    sta $d021
    lda #9
    sta $d022
    lda #15
    sta $d023
	lda #((Screen>>6)&$f0) | ((Chars>>10)&$0e)
	sta $d018

    lda #<ColorSrc
    sta 2
    lda #>ColorSrc
    sta 3
    lda #<ColorDst
    sta 4
    lda #>ColorDst
    sta 5

    ldy #0
    ldx #0
    {
        lda (2),y
        sta (4),y
        iny
        lda (2),y
        sta (4),y
        iny
        lda (2),y
        sta (4),y
        iny
        lda (2),y
        sta (4),y
        iny
        {
            bne %
            inc 3
            inc 5
        }
        inx
        bne !
    }
    beq *
}

eval ColorSrc - *
