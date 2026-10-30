; Research-only trace thunks (generated). Each thunk saves the argument
; registers, calls TraceLog(index), restores them and jumps to the original
; function, so the stack and arguments are exactly as the caller left them.

PUBLIC g_traceThunks
PUBLIC g_traceOrig
EXTERN TraceLog:PROC

.data
ALIGN 8
g_traceOrig QWORD 256 DUP(0)
g_traceThunks LABEL QWORD
    QWORD TraceThunk0
    QWORD TraceThunk1
    QWORD TraceThunk2
    QWORD TraceThunk3
    QWORD TraceThunk4
    QWORD TraceThunk5
    QWORD TraceThunk6
    QWORD TraceThunk7
    QWORD TraceThunk8
    QWORD TraceThunk9
    QWORD TraceThunk10
    QWORD TraceThunk11
    QWORD TraceThunk12
    QWORD TraceThunk13
    QWORD TraceThunk14
    QWORD TraceThunk15
    QWORD TraceThunk16
    QWORD TraceThunk17
    QWORD TraceThunk18
    QWORD TraceThunk19
    QWORD TraceThunk20
    QWORD TraceThunk21
    QWORD TraceThunk22
    QWORD TraceThunk23
    QWORD TraceThunk24
    QWORD TraceThunk25
    QWORD TraceThunk26
    QWORD TraceThunk27
    QWORD TraceThunk28
    QWORD TraceThunk29
    QWORD TraceThunk30
    QWORD TraceThunk31
    QWORD TraceThunk32
    QWORD TraceThunk33
    QWORD TraceThunk34
    QWORD TraceThunk35
    QWORD TraceThunk36
    QWORD TraceThunk37
    QWORD TraceThunk38
    QWORD TraceThunk39
    QWORD TraceThunk40
    QWORD TraceThunk41
    QWORD TraceThunk42
    QWORD TraceThunk43
    QWORD TraceThunk44
    QWORD TraceThunk45
    QWORD TraceThunk46
    QWORD TraceThunk47
    QWORD TraceThunk48
    QWORD TraceThunk49
    QWORD TraceThunk50
    QWORD TraceThunk51
    QWORD TraceThunk52
    QWORD TraceThunk53
    QWORD TraceThunk54
    QWORD TraceThunk55
    QWORD TraceThunk56
    QWORD TraceThunk57
    QWORD TraceThunk58
    QWORD TraceThunk59
    QWORD TraceThunk60
    QWORD TraceThunk61
    QWORD TraceThunk62
    QWORD TraceThunk63
    QWORD TraceThunk64
    QWORD TraceThunk65
    QWORD TraceThunk66
    QWORD TraceThunk67
    QWORD TraceThunk68
    QWORD TraceThunk69
    QWORD TraceThunk70
    QWORD TraceThunk71
    QWORD TraceThunk72
    QWORD TraceThunk73
    QWORD TraceThunk74
    QWORD TraceThunk75
    QWORD TraceThunk76
    QWORD TraceThunk77
    QWORD TraceThunk78
    QWORD TraceThunk79
    QWORD TraceThunk80
    QWORD TraceThunk81
    QWORD TraceThunk82
    QWORD TraceThunk83
    QWORD TraceThunk84
    QWORD TraceThunk85
    QWORD TraceThunk86
    QWORD TraceThunk87
    QWORD TraceThunk88
    QWORD TraceThunk89
    QWORD TraceThunk90
    QWORD TraceThunk91
    QWORD TraceThunk92
    QWORD TraceThunk93
    QWORD TraceThunk94
    QWORD TraceThunk95
    QWORD TraceThunk96
    QWORD TraceThunk97
    QWORD TraceThunk98
    QWORD TraceThunk99
    QWORD TraceThunk100
    QWORD TraceThunk101
    QWORD TraceThunk102
    QWORD TraceThunk103
    QWORD TraceThunk104
    QWORD TraceThunk105
    QWORD TraceThunk106
    QWORD TraceThunk107
    QWORD TraceThunk108
    QWORD TraceThunk109
    QWORD TraceThunk110
    QWORD TraceThunk111
    QWORD TraceThunk112
    QWORD TraceThunk113
    QWORD TraceThunk114
    QWORD TraceThunk115
    QWORD TraceThunk116
    QWORD TraceThunk117
    QWORD TraceThunk118
    QWORD TraceThunk119
    QWORD TraceThunk120
    QWORD TraceThunk121
    QWORD TraceThunk122
    QWORD TraceThunk123
    QWORD TraceThunk124
    QWORD TraceThunk125
    QWORD TraceThunk126
    QWORD TraceThunk127
    QWORD TraceThunk128
    QWORD TraceThunk129
    QWORD TraceThunk130
    QWORD TraceThunk131
    QWORD TraceThunk132
    QWORD TraceThunk133
    QWORD TraceThunk134
    QWORD TraceThunk135
    QWORD TraceThunk136
    QWORD TraceThunk137
    QWORD TraceThunk138
    QWORD TraceThunk139
    QWORD TraceThunk140
    QWORD TraceThunk141
    QWORD TraceThunk142
    QWORD TraceThunk143
    QWORD TraceThunk144
    QWORD TraceThunk145
    QWORD TraceThunk146
    QWORD TraceThunk147
    QWORD TraceThunk148
    QWORD TraceThunk149
    QWORD TraceThunk150
    QWORD TraceThunk151
    QWORD TraceThunk152
    QWORD TraceThunk153
    QWORD TraceThunk154
    QWORD TraceThunk155
    QWORD TraceThunk156
    QWORD TraceThunk157
    QWORD TraceThunk158
    QWORD TraceThunk159
    QWORD TraceThunk160
    QWORD TraceThunk161
    QWORD TraceThunk162
    QWORD TraceThunk163
    QWORD TraceThunk164
    QWORD TraceThunk165
    QWORD TraceThunk166
    QWORD TraceThunk167
    QWORD TraceThunk168
    QWORD TraceThunk169
    QWORD TraceThunk170
    QWORD TraceThunk171
    QWORD TraceThunk172
    QWORD TraceThunk173
    QWORD TraceThunk174
    QWORD TraceThunk175
    QWORD TraceThunk176
    QWORD TraceThunk177
    QWORD TraceThunk178
    QWORD TraceThunk179
    QWORD TraceThunk180
    QWORD TraceThunk181
    QWORD TraceThunk182
    QWORD TraceThunk183
    QWORD TraceThunk184
    QWORD TraceThunk185
    QWORD TraceThunk186
    QWORD TraceThunk187
    QWORD TraceThunk188
    QWORD TraceThunk189
    QWORD TraceThunk190
    QWORD TraceThunk191
    QWORD TraceThunk192
    QWORD TraceThunk193
    QWORD TraceThunk194
    QWORD TraceThunk195
    QWORD TraceThunk196
    QWORD TraceThunk197
    QWORD TraceThunk198
    QWORD TraceThunk199
    QWORD TraceThunk200
    QWORD TraceThunk201
    QWORD TraceThunk202
    QWORD TraceThunk203
    QWORD TraceThunk204
    QWORD TraceThunk205
    QWORD TraceThunk206
    QWORD TraceThunk207
    QWORD TraceThunk208
    QWORD TraceThunk209
    QWORD TraceThunk210
    QWORD TraceThunk211
    QWORD TraceThunk212
    QWORD TraceThunk213
    QWORD TraceThunk214
    QWORD TraceThunk215
    QWORD TraceThunk216
    QWORD TraceThunk217
    QWORD TraceThunk218
    QWORD TraceThunk219
    QWORD TraceThunk220
    QWORD TraceThunk221
    QWORD TraceThunk222
    QWORD TraceThunk223
    QWORD TraceThunk224
    QWORD TraceThunk225
    QWORD TraceThunk226
    QWORD TraceThunk227
    QWORD TraceThunk228
    QWORD TraceThunk229
    QWORD TraceThunk230
    QWORD TraceThunk231
    QWORD TraceThunk232
    QWORD TraceThunk233
    QWORD TraceThunk234
    QWORD TraceThunk235
    QWORD TraceThunk236
    QWORD TraceThunk237
    QWORD TraceThunk238
    QWORD TraceThunk239
    QWORD TraceThunk240
    QWORD TraceThunk241
    QWORD TraceThunk242
    QWORD TraceThunk243
    QWORD TraceThunk244
    QWORD TraceThunk245
    QWORD TraceThunk246
    QWORD TraceThunk247
    QWORD TraceThunk248
    QWORD TraceThunk249
    QWORD TraceThunk250
    QWORD TraceThunk251
    QWORD TraceThunk252
    QWORD TraceThunk253
    QWORD TraceThunk254
    QWORD TraceThunk255

.code
TraceCommon PROC
    push rcx
    push rdx
    push r8
    push r9
    push r10
    sub rsp, 60h
    movdqu xmmword ptr [rsp+20h], xmm0
    movdqu xmmword ptr [rsp+30h], xmm1
    movdqu xmmword ptr [rsp+40h], xmm2
    movdqu xmmword ptr [rsp+50h], xmm3
    mov rcx, r10
    call TraceLog
    movdqu xmm0, xmmword ptr [rsp+20h]
    movdqu xmm1, xmmword ptr [rsp+30h]
    movdqu xmm2, xmmword ptr [rsp+40h]
    movdqu xmm3, xmmword ptr [rsp+50h]
    add rsp, 60h
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    lea rax, g_traceOrig
    jmp qword ptr [rax + r10*8]
TraceCommon ENDP

TraceThunk0 PROC
    mov r10, 0
    jmp TraceCommon
TraceThunk0 ENDP
TraceThunk1 PROC
    mov r10, 1
    jmp TraceCommon
TraceThunk1 ENDP
TraceThunk2 PROC
    mov r10, 2
    jmp TraceCommon
TraceThunk2 ENDP
TraceThunk3 PROC
    mov r10, 3
    jmp TraceCommon
TraceThunk3 ENDP
TraceThunk4 PROC
    mov r10, 4
    jmp TraceCommon
TraceThunk4 ENDP
TraceThunk5 PROC
    mov r10, 5
    jmp TraceCommon
TraceThunk5 ENDP
TraceThunk6 PROC
    mov r10, 6
    jmp TraceCommon
TraceThunk6 ENDP
TraceThunk7 PROC
    mov r10, 7
    jmp TraceCommon
TraceThunk7 ENDP
TraceThunk8 PROC
    mov r10, 8
    jmp TraceCommon
TraceThunk8 ENDP
TraceThunk9 PROC
    mov r10, 9
    jmp TraceCommon
TraceThunk9 ENDP
TraceThunk10 PROC
    mov r10, 10
    jmp TraceCommon
TraceThunk10 ENDP
TraceThunk11 PROC
    mov r10, 11
    jmp TraceCommon
TraceThunk11 ENDP
TraceThunk12 PROC
    mov r10, 12
    jmp TraceCommon
TraceThunk12 ENDP
TraceThunk13 PROC
    mov r10, 13
    jmp TraceCommon
TraceThunk13 ENDP
TraceThunk14 PROC
    mov r10, 14
    jmp TraceCommon
TraceThunk14 ENDP
TraceThunk15 PROC
    mov r10, 15
    jmp TraceCommon
TraceThunk15 ENDP
TraceThunk16 PROC
    mov r10, 16
    jmp TraceCommon
TraceThunk16 ENDP
TraceThunk17 PROC
    mov r10, 17
    jmp TraceCommon
TraceThunk17 ENDP
TraceThunk18 PROC
    mov r10, 18
    jmp TraceCommon
TraceThunk18 ENDP
TraceThunk19 PROC
    mov r10, 19
    jmp TraceCommon
TraceThunk19 ENDP
TraceThunk20 PROC
    mov r10, 20
    jmp TraceCommon
TraceThunk20 ENDP
TraceThunk21 PROC
    mov r10, 21
    jmp TraceCommon
TraceThunk21 ENDP
TraceThunk22 PROC
    mov r10, 22
    jmp TraceCommon
TraceThunk22 ENDP
TraceThunk23 PROC
    mov r10, 23
    jmp TraceCommon
TraceThunk23 ENDP
TraceThunk24 PROC
    mov r10, 24
    jmp TraceCommon
TraceThunk24 ENDP
TraceThunk25 PROC
    mov r10, 25
    jmp TraceCommon
TraceThunk25 ENDP
TraceThunk26 PROC
    mov r10, 26
    jmp TraceCommon
TraceThunk26 ENDP
TraceThunk27 PROC
    mov r10, 27
    jmp TraceCommon
TraceThunk27 ENDP
TraceThunk28 PROC
    mov r10, 28
    jmp TraceCommon
TraceThunk28 ENDP
TraceThunk29 PROC
    mov r10, 29
    jmp TraceCommon
TraceThunk29 ENDP
TraceThunk30 PROC
    mov r10, 30
    jmp TraceCommon
TraceThunk30 ENDP
TraceThunk31 PROC
    mov r10, 31
    jmp TraceCommon
TraceThunk31 ENDP
TraceThunk32 PROC
    mov r10, 32
    jmp TraceCommon
TraceThunk32 ENDP
TraceThunk33 PROC
    mov r10, 33
    jmp TraceCommon
TraceThunk33 ENDP
TraceThunk34 PROC
    mov r10, 34
    jmp TraceCommon
TraceThunk34 ENDP
TraceThunk35 PROC
    mov r10, 35
    jmp TraceCommon
TraceThunk35 ENDP
TraceThunk36 PROC
    mov r10, 36
    jmp TraceCommon
TraceThunk36 ENDP
TraceThunk37 PROC
    mov r10, 37
    jmp TraceCommon
TraceThunk37 ENDP
TraceThunk38 PROC
    mov r10, 38
    jmp TraceCommon
TraceThunk38 ENDP
TraceThunk39 PROC
    mov r10, 39
    jmp TraceCommon
TraceThunk39 ENDP
TraceThunk40 PROC
    mov r10, 40
    jmp TraceCommon
TraceThunk40 ENDP
TraceThunk41 PROC
    mov r10, 41
    jmp TraceCommon
TraceThunk41 ENDP
TraceThunk42 PROC
    mov r10, 42
    jmp TraceCommon
TraceThunk42 ENDP
TraceThunk43 PROC
    mov r10, 43
    jmp TraceCommon
TraceThunk43 ENDP
TraceThunk44 PROC
    mov r10, 44
    jmp TraceCommon
TraceThunk44 ENDP
TraceThunk45 PROC
    mov r10, 45
    jmp TraceCommon
TraceThunk45 ENDP
TraceThunk46 PROC
    mov r10, 46
    jmp TraceCommon
TraceThunk46 ENDP
TraceThunk47 PROC
    mov r10, 47
    jmp TraceCommon
TraceThunk47 ENDP
TraceThunk48 PROC
    mov r10, 48
    jmp TraceCommon
TraceThunk48 ENDP
TraceThunk49 PROC
    mov r10, 49
    jmp TraceCommon
TraceThunk49 ENDP
TraceThunk50 PROC
    mov r10, 50
    jmp TraceCommon
TraceThunk50 ENDP
TraceThunk51 PROC
    mov r10, 51
    jmp TraceCommon
TraceThunk51 ENDP
TraceThunk52 PROC
    mov r10, 52
    jmp TraceCommon
TraceThunk52 ENDP
TraceThunk53 PROC
    mov r10, 53
    jmp TraceCommon
TraceThunk53 ENDP
TraceThunk54 PROC
    mov r10, 54
    jmp TraceCommon
TraceThunk54 ENDP
TraceThunk55 PROC
    mov r10, 55
    jmp TraceCommon
TraceThunk55 ENDP
TraceThunk56 PROC
    mov r10, 56
    jmp TraceCommon
TraceThunk56 ENDP
TraceThunk57 PROC
    mov r10, 57
    jmp TraceCommon
TraceThunk57 ENDP
TraceThunk58 PROC
    mov r10, 58
    jmp TraceCommon
TraceThunk58 ENDP
TraceThunk59 PROC
    mov r10, 59
    jmp TraceCommon
TraceThunk59 ENDP
TraceThunk60 PROC
    mov r10, 60
    jmp TraceCommon
TraceThunk60 ENDP
TraceThunk61 PROC
    mov r10, 61
    jmp TraceCommon
TraceThunk61 ENDP
TraceThunk62 PROC
    mov r10, 62
    jmp TraceCommon
TraceThunk62 ENDP
TraceThunk63 PROC
    mov r10, 63
    jmp TraceCommon
TraceThunk63 ENDP
TraceThunk64 PROC
    mov r10, 64
    jmp TraceCommon
TraceThunk64 ENDP
TraceThunk65 PROC
    mov r10, 65
    jmp TraceCommon
TraceThunk65 ENDP
TraceThunk66 PROC
    mov r10, 66
    jmp TraceCommon
TraceThunk66 ENDP
TraceThunk67 PROC
    mov r10, 67
    jmp TraceCommon
TraceThunk67 ENDP
TraceThunk68 PROC
    mov r10, 68
    jmp TraceCommon
TraceThunk68 ENDP
TraceThunk69 PROC
    mov r10, 69
    jmp TraceCommon
TraceThunk69 ENDP
TraceThunk70 PROC
    mov r10, 70
    jmp TraceCommon
TraceThunk70 ENDP
TraceThunk71 PROC
    mov r10, 71
    jmp TraceCommon
TraceThunk71 ENDP
TraceThunk72 PROC
    mov r10, 72
    jmp TraceCommon
TraceThunk72 ENDP
TraceThunk73 PROC
    mov r10, 73
    jmp TraceCommon
TraceThunk73 ENDP
TraceThunk74 PROC
    mov r10, 74
    jmp TraceCommon
TraceThunk74 ENDP
TraceThunk75 PROC
    mov r10, 75
    jmp TraceCommon
TraceThunk75 ENDP
TraceThunk76 PROC
    mov r10, 76
    jmp TraceCommon
TraceThunk76 ENDP
TraceThunk77 PROC
    mov r10, 77
    jmp TraceCommon
TraceThunk77 ENDP
TraceThunk78 PROC
    mov r10, 78
    jmp TraceCommon
TraceThunk78 ENDP
TraceThunk79 PROC
    mov r10, 79
    jmp TraceCommon
TraceThunk79 ENDP
TraceThunk80 PROC
    mov r10, 80
    jmp TraceCommon
TraceThunk80 ENDP
TraceThunk81 PROC
    mov r10, 81
    jmp TraceCommon
TraceThunk81 ENDP
TraceThunk82 PROC
    mov r10, 82
    jmp TraceCommon
TraceThunk82 ENDP
TraceThunk83 PROC
    mov r10, 83
    jmp TraceCommon
TraceThunk83 ENDP
TraceThunk84 PROC
    mov r10, 84
    jmp TraceCommon
TraceThunk84 ENDP
TraceThunk85 PROC
    mov r10, 85
    jmp TraceCommon
TraceThunk85 ENDP
TraceThunk86 PROC
    mov r10, 86
    jmp TraceCommon
TraceThunk86 ENDP
TraceThunk87 PROC
    mov r10, 87
    jmp TraceCommon
TraceThunk87 ENDP
TraceThunk88 PROC
    mov r10, 88
    jmp TraceCommon
TraceThunk88 ENDP
TraceThunk89 PROC
    mov r10, 89
    jmp TraceCommon
TraceThunk89 ENDP
TraceThunk90 PROC
    mov r10, 90
    jmp TraceCommon
TraceThunk90 ENDP
TraceThunk91 PROC
    mov r10, 91
    jmp TraceCommon
TraceThunk91 ENDP
TraceThunk92 PROC
    mov r10, 92
    jmp TraceCommon
TraceThunk92 ENDP
TraceThunk93 PROC
    mov r10, 93
    jmp TraceCommon
TraceThunk93 ENDP
TraceThunk94 PROC
    mov r10, 94
    jmp TraceCommon
TraceThunk94 ENDP
TraceThunk95 PROC
    mov r10, 95
    jmp TraceCommon
TraceThunk95 ENDP
TraceThunk96 PROC
    mov r10, 96
    jmp TraceCommon
TraceThunk96 ENDP
TraceThunk97 PROC
    mov r10, 97
    jmp TraceCommon
TraceThunk97 ENDP
TraceThunk98 PROC
    mov r10, 98
    jmp TraceCommon
TraceThunk98 ENDP
TraceThunk99 PROC
    mov r10, 99
    jmp TraceCommon
TraceThunk99 ENDP
TraceThunk100 PROC
    mov r10, 100
    jmp TraceCommon
TraceThunk100 ENDP
TraceThunk101 PROC
    mov r10, 101
    jmp TraceCommon
TraceThunk101 ENDP
TraceThunk102 PROC
    mov r10, 102
    jmp TraceCommon
TraceThunk102 ENDP
TraceThunk103 PROC
    mov r10, 103
    jmp TraceCommon
TraceThunk103 ENDP
TraceThunk104 PROC
    mov r10, 104
    jmp TraceCommon
TraceThunk104 ENDP
TraceThunk105 PROC
    mov r10, 105
    jmp TraceCommon
TraceThunk105 ENDP
TraceThunk106 PROC
    mov r10, 106
    jmp TraceCommon
TraceThunk106 ENDP
TraceThunk107 PROC
    mov r10, 107
    jmp TraceCommon
TraceThunk107 ENDP
TraceThunk108 PROC
    mov r10, 108
    jmp TraceCommon
TraceThunk108 ENDP
TraceThunk109 PROC
    mov r10, 109
    jmp TraceCommon
TraceThunk109 ENDP
TraceThunk110 PROC
    mov r10, 110
    jmp TraceCommon
TraceThunk110 ENDP
TraceThunk111 PROC
    mov r10, 111
    jmp TraceCommon
TraceThunk111 ENDP
TraceThunk112 PROC
    mov r10, 112
    jmp TraceCommon
TraceThunk112 ENDP
TraceThunk113 PROC
    mov r10, 113
    jmp TraceCommon
TraceThunk113 ENDP
TraceThunk114 PROC
    mov r10, 114
    jmp TraceCommon
TraceThunk114 ENDP
TraceThunk115 PROC
    mov r10, 115
    jmp TraceCommon
TraceThunk115 ENDP
TraceThunk116 PROC
    mov r10, 116
    jmp TraceCommon
TraceThunk116 ENDP
TraceThunk117 PROC
    mov r10, 117
    jmp TraceCommon
TraceThunk117 ENDP
TraceThunk118 PROC
    mov r10, 118
    jmp TraceCommon
TraceThunk118 ENDP
TraceThunk119 PROC
    mov r10, 119
    jmp TraceCommon
TraceThunk119 ENDP
TraceThunk120 PROC
    mov r10, 120
    jmp TraceCommon
TraceThunk120 ENDP
TraceThunk121 PROC
    mov r10, 121
    jmp TraceCommon
TraceThunk121 ENDP
TraceThunk122 PROC
    mov r10, 122
    jmp TraceCommon
TraceThunk122 ENDP
TraceThunk123 PROC
    mov r10, 123
    jmp TraceCommon
TraceThunk123 ENDP
TraceThunk124 PROC
    mov r10, 124
    jmp TraceCommon
TraceThunk124 ENDP
TraceThunk125 PROC
    mov r10, 125
    jmp TraceCommon
TraceThunk125 ENDP
TraceThunk126 PROC
    mov r10, 126
    jmp TraceCommon
TraceThunk126 ENDP
TraceThunk127 PROC
    mov r10, 127
    jmp TraceCommon
TraceThunk127 ENDP
TraceThunk128 PROC
    mov r10, 128
    jmp TraceCommon
TraceThunk128 ENDP
TraceThunk129 PROC
    mov r10, 129
    jmp TraceCommon
TraceThunk129 ENDP
TraceThunk130 PROC
    mov r10, 130
    jmp TraceCommon
TraceThunk130 ENDP
TraceThunk131 PROC
    mov r10, 131
    jmp TraceCommon
TraceThunk131 ENDP
TraceThunk132 PROC
    mov r10, 132
    jmp TraceCommon
TraceThunk132 ENDP
TraceThunk133 PROC
    mov r10, 133
    jmp TraceCommon
TraceThunk133 ENDP
TraceThunk134 PROC
    mov r10, 134
    jmp TraceCommon
TraceThunk134 ENDP
TraceThunk135 PROC
    mov r10, 135
    jmp TraceCommon
TraceThunk135 ENDP
TraceThunk136 PROC
    mov r10, 136
    jmp TraceCommon
TraceThunk136 ENDP
TraceThunk137 PROC
    mov r10, 137
    jmp TraceCommon
TraceThunk137 ENDP
TraceThunk138 PROC
    mov r10, 138
    jmp TraceCommon
TraceThunk138 ENDP
TraceThunk139 PROC
    mov r10, 139
    jmp TraceCommon
TraceThunk139 ENDP
TraceThunk140 PROC
    mov r10, 140
    jmp TraceCommon
TraceThunk140 ENDP
TraceThunk141 PROC
    mov r10, 141
    jmp TraceCommon
TraceThunk141 ENDP
TraceThunk142 PROC
    mov r10, 142
    jmp TraceCommon
TraceThunk142 ENDP
TraceThunk143 PROC
    mov r10, 143
    jmp TraceCommon
TraceThunk143 ENDP
TraceThunk144 PROC
    mov r10, 144
    jmp TraceCommon
TraceThunk144 ENDP
TraceThunk145 PROC
    mov r10, 145
    jmp TraceCommon
TraceThunk145 ENDP
TraceThunk146 PROC
    mov r10, 146
    jmp TraceCommon
TraceThunk146 ENDP
TraceThunk147 PROC
    mov r10, 147
    jmp TraceCommon
TraceThunk147 ENDP
TraceThunk148 PROC
    mov r10, 148
    jmp TraceCommon
TraceThunk148 ENDP
TraceThunk149 PROC
    mov r10, 149
    jmp TraceCommon
TraceThunk149 ENDP
TraceThunk150 PROC
    mov r10, 150
    jmp TraceCommon
TraceThunk150 ENDP
TraceThunk151 PROC
    mov r10, 151
    jmp TraceCommon
TraceThunk151 ENDP
TraceThunk152 PROC
    mov r10, 152
    jmp TraceCommon
TraceThunk152 ENDP
TraceThunk153 PROC
    mov r10, 153
    jmp TraceCommon
TraceThunk153 ENDP
TraceThunk154 PROC
    mov r10, 154
    jmp TraceCommon
TraceThunk154 ENDP
TraceThunk155 PROC
    mov r10, 155
    jmp TraceCommon
TraceThunk155 ENDP
TraceThunk156 PROC
    mov r10, 156
    jmp TraceCommon
TraceThunk156 ENDP
TraceThunk157 PROC
    mov r10, 157
    jmp TraceCommon
TraceThunk157 ENDP
TraceThunk158 PROC
    mov r10, 158
    jmp TraceCommon
TraceThunk158 ENDP
TraceThunk159 PROC
    mov r10, 159
    jmp TraceCommon
TraceThunk159 ENDP
TraceThunk160 PROC
    mov r10, 160
    jmp TraceCommon
TraceThunk160 ENDP
TraceThunk161 PROC
    mov r10, 161
    jmp TraceCommon
TraceThunk161 ENDP
TraceThunk162 PROC
    mov r10, 162
    jmp TraceCommon
TraceThunk162 ENDP
TraceThunk163 PROC
    mov r10, 163
    jmp TraceCommon
TraceThunk163 ENDP
TraceThunk164 PROC
    mov r10, 164
    jmp TraceCommon
TraceThunk164 ENDP
TraceThunk165 PROC
    mov r10, 165
    jmp TraceCommon
TraceThunk165 ENDP
TraceThunk166 PROC
    mov r10, 166
    jmp TraceCommon
TraceThunk166 ENDP
TraceThunk167 PROC
    mov r10, 167
    jmp TraceCommon
TraceThunk167 ENDP
TraceThunk168 PROC
    mov r10, 168
    jmp TraceCommon
TraceThunk168 ENDP
TraceThunk169 PROC
    mov r10, 169
    jmp TraceCommon
TraceThunk169 ENDP
TraceThunk170 PROC
    mov r10, 170
    jmp TraceCommon
TraceThunk170 ENDP
TraceThunk171 PROC
    mov r10, 171
    jmp TraceCommon
TraceThunk171 ENDP
TraceThunk172 PROC
    mov r10, 172
    jmp TraceCommon
TraceThunk172 ENDP
TraceThunk173 PROC
    mov r10, 173
    jmp TraceCommon
TraceThunk173 ENDP
TraceThunk174 PROC
    mov r10, 174
    jmp TraceCommon
TraceThunk174 ENDP
TraceThunk175 PROC
    mov r10, 175
    jmp TraceCommon
TraceThunk175 ENDP
TraceThunk176 PROC
    mov r10, 176
    jmp TraceCommon
TraceThunk176 ENDP
TraceThunk177 PROC
    mov r10, 177
    jmp TraceCommon
TraceThunk177 ENDP
TraceThunk178 PROC
    mov r10, 178
    jmp TraceCommon
TraceThunk178 ENDP
TraceThunk179 PROC
    mov r10, 179
    jmp TraceCommon
TraceThunk179 ENDP
TraceThunk180 PROC
    mov r10, 180
    jmp TraceCommon
TraceThunk180 ENDP
TraceThunk181 PROC
    mov r10, 181
    jmp TraceCommon
TraceThunk181 ENDP
TraceThunk182 PROC
    mov r10, 182
    jmp TraceCommon
TraceThunk182 ENDP
TraceThunk183 PROC
    mov r10, 183
    jmp TraceCommon
TraceThunk183 ENDP
TraceThunk184 PROC
    mov r10, 184
    jmp TraceCommon
TraceThunk184 ENDP
TraceThunk185 PROC
    mov r10, 185
    jmp TraceCommon
TraceThunk185 ENDP
TraceThunk186 PROC
    mov r10, 186
    jmp TraceCommon
TraceThunk186 ENDP
TraceThunk187 PROC
    mov r10, 187
    jmp TraceCommon
TraceThunk187 ENDP
TraceThunk188 PROC
    mov r10, 188
    jmp TraceCommon
TraceThunk188 ENDP
TraceThunk189 PROC
    mov r10, 189
    jmp TraceCommon
TraceThunk189 ENDP
TraceThunk190 PROC
    mov r10, 190
    jmp TraceCommon
TraceThunk190 ENDP
TraceThunk191 PROC
    mov r10, 191
    jmp TraceCommon
TraceThunk191 ENDP
TraceThunk192 PROC
    mov r10, 192
    jmp TraceCommon
TraceThunk192 ENDP
TraceThunk193 PROC
    mov r10, 193
    jmp TraceCommon
TraceThunk193 ENDP
TraceThunk194 PROC
    mov r10, 194
    jmp TraceCommon
TraceThunk194 ENDP
TraceThunk195 PROC
    mov r10, 195
    jmp TraceCommon
TraceThunk195 ENDP
TraceThunk196 PROC
    mov r10, 196
    jmp TraceCommon
TraceThunk196 ENDP
TraceThunk197 PROC
    mov r10, 197
    jmp TraceCommon
TraceThunk197 ENDP
TraceThunk198 PROC
    mov r10, 198
    jmp TraceCommon
TraceThunk198 ENDP
TraceThunk199 PROC
    mov r10, 199
    jmp TraceCommon
TraceThunk199 ENDP
TraceThunk200 PROC
    mov r10, 200
    jmp TraceCommon
TraceThunk200 ENDP
TraceThunk201 PROC
    mov r10, 201
    jmp TraceCommon
TraceThunk201 ENDP
TraceThunk202 PROC
    mov r10, 202
    jmp TraceCommon
TraceThunk202 ENDP
TraceThunk203 PROC
    mov r10, 203
    jmp TraceCommon
TraceThunk203 ENDP
TraceThunk204 PROC
    mov r10, 204
    jmp TraceCommon
TraceThunk204 ENDP
TraceThunk205 PROC
    mov r10, 205
    jmp TraceCommon
TraceThunk205 ENDP
TraceThunk206 PROC
    mov r10, 206
    jmp TraceCommon
TraceThunk206 ENDP
TraceThunk207 PROC
    mov r10, 207
    jmp TraceCommon
TraceThunk207 ENDP
TraceThunk208 PROC
    mov r10, 208
    jmp TraceCommon
TraceThunk208 ENDP
TraceThunk209 PROC
    mov r10, 209
    jmp TraceCommon
TraceThunk209 ENDP
TraceThunk210 PROC
    mov r10, 210
    jmp TraceCommon
TraceThunk210 ENDP
TraceThunk211 PROC
    mov r10, 211
    jmp TraceCommon
TraceThunk211 ENDP
TraceThunk212 PROC
    mov r10, 212
    jmp TraceCommon
TraceThunk212 ENDP
TraceThunk213 PROC
    mov r10, 213
    jmp TraceCommon
TraceThunk213 ENDP
TraceThunk214 PROC
    mov r10, 214
    jmp TraceCommon
TraceThunk214 ENDP
TraceThunk215 PROC
    mov r10, 215
    jmp TraceCommon
TraceThunk215 ENDP
TraceThunk216 PROC
    mov r10, 216
    jmp TraceCommon
TraceThunk216 ENDP
TraceThunk217 PROC
    mov r10, 217
    jmp TraceCommon
TraceThunk217 ENDP
TraceThunk218 PROC
    mov r10, 218
    jmp TraceCommon
TraceThunk218 ENDP
TraceThunk219 PROC
    mov r10, 219
    jmp TraceCommon
TraceThunk219 ENDP
TraceThunk220 PROC
    mov r10, 220
    jmp TraceCommon
TraceThunk220 ENDP
TraceThunk221 PROC
    mov r10, 221
    jmp TraceCommon
TraceThunk221 ENDP
TraceThunk222 PROC
    mov r10, 222
    jmp TraceCommon
TraceThunk222 ENDP
TraceThunk223 PROC
    mov r10, 223
    jmp TraceCommon
TraceThunk223 ENDP
TraceThunk224 PROC
    mov r10, 224
    jmp TraceCommon
TraceThunk224 ENDP
TraceThunk225 PROC
    mov r10, 225
    jmp TraceCommon
TraceThunk225 ENDP
TraceThunk226 PROC
    mov r10, 226
    jmp TraceCommon
TraceThunk226 ENDP
TraceThunk227 PROC
    mov r10, 227
    jmp TraceCommon
TraceThunk227 ENDP
TraceThunk228 PROC
    mov r10, 228
    jmp TraceCommon
TraceThunk228 ENDP
TraceThunk229 PROC
    mov r10, 229
    jmp TraceCommon
TraceThunk229 ENDP
TraceThunk230 PROC
    mov r10, 230
    jmp TraceCommon
TraceThunk230 ENDP
TraceThunk231 PROC
    mov r10, 231
    jmp TraceCommon
TraceThunk231 ENDP
TraceThunk232 PROC
    mov r10, 232
    jmp TraceCommon
TraceThunk232 ENDP
TraceThunk233 PROC
    mov r10, 233
    jmp TraceCommon
TraceThunk233 ENDP
TraceThunk234 PROC
    mov r10, 234
    jmp TraceCommon
TraceThunk234 ENDP
TraceThunk235 PROC
    mov r10, 235
    jmp TraceCommon
TraceThunk235 ENDP
TraceThunk236 PROC
    mov r10, 236
    jmp TraceCommon
TraceThunk236 ENDP
TraceThunk237 PROC
    mov r10, 237
    jmp TraceCommon
TraceThunk237 ENDP
TraceThunk238 PROC
    mov r10, 238
    jmp TraceCommon
TraceThunk238 ENDP
TraceThunk239 PROC
    mov r10, 239
    jmp TraceCommon
TraceThunk239 ENDP
TraceThunk240 PROC
    mov r10, 240
    jmp TraceCommon
TraceThunk240 ENDP
TraceThunk241 PROC
    mov r10, 241
    jmp TraceCommon
TraceThunk241 ENDP
TraceThunk242 PROC
    mov r10, 242
    jmp TraceCommon
TraceThunk242 ENDP
TraceThunk243 PROC
    mov r10, 243
    jmp TraceCommon
TraceThunk243 ENDP
TraceThunk244 PROC
    mov r10, 244
    jmp TraceCommon
TraceThunk244 ENDP
TraceThunk245 PROC
    mov r10, 245
    jmp TraceCommon
TraceThunk245 ENDP
TraceThunk246 PROC
    mov r10, 246
    jmp TraceCommon
TraceThunk246 ENDP
TraceThunk247 PROC
    mov r10, 247
    jmp TraceCommon
TraceThunk247 ENDP
TraceThunk248 PROC
    mov r10, 248
    jmp TraceCommon
TraceThunk248 ENDP
TraceThunk249 PROC
    mov r10, 249
    jmp TraceCommon
TraceThunk249 ENDP
TraceThunk250 PROC
    mov r10, 250
    jmp TraceCommon
TraceThunk250 ENDP
TraceThunk251 PROC
    mov r10, 251
    jmp TraceCommon
TraceThunk251 ENDP
TraceThunk252 PROC
    mov r10, 252
    jmp TraceCommon
TraceThunk252 ENDP
TraceThunk253 PROC
    mov r10, 253
    jmp TraceCommon
TraceThunk253 ENDP
TraceThunk254 PROC
    mov r10, 254
    jmp TraceCommon
TraceThunk254 ENDP
TraceThunk255 PROC
    mov r10, 255
    jmp TraceCommon
TraceThunk255 ENDP

END
