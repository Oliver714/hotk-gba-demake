.section .rodata.music_adpcm,"a",%progbits
.balign 4
.global music_world_adpcm
.type music_world_adpcm, %object
music_world_adpcm:
.incbin "../generated/music_world.adpcm"
.size music_world_adpcm, .-music_world_adpcm
.balign 4
.global music_battle_adpcm
.type music_battle_adpcm, %object
music_battle_adpcm:
.incbin "../generated/music_battle.adpcm"
.size music_battle_adpcm, .-music_battle_adpcm
.section .note.GNU-stack,"",%progbits
