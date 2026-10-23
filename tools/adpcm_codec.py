"""Independent512-sample IMA-style blocks: predictor/index header +511 nibbles."""
import struct
STEPS=(7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767)
INDEX=(-1,-1,-1,-1,2,4,6,8)
SAMPLES=512
BYTES=260
def transition(index,code):
    step=STEPS[index]
    delta=(step>>3)+(step if code&4 else 0)+(step>>1 if code&2 else 0)+(step>>2 if code&1 else 0)
    return (-delta if code&8 else delta),max(0,min(88,index+INDEX[code&7]))
def encode(samples):
    assert len(samples)%SAMPLES==0
    data=bytearray(); index=0
    for start in range(0,len(samples),SAMPLES):
        predictor=samples[start]
        data+=struct.pack('<hBB',predictor,index,0)
        codes=[]
        for target in samples[start+1:start+SAMPLES]:
            step=STEPS[index]; difference=target-predictor
            code=8 if difference<0 else 0; difference=abs(difference)
            for bit,part in ((4,step),(2,step>>1),(1,step>>2)):
                if difference>=part: code|=bit; difference-=part
            delta,index=transition(index,code)
            predictor=max(-32768,min(32767,predictor+delta)); codes.append(code)
        codes.append(0)
        data+=bytes(codes[i]|(codes[i+1]<<4) for i in range(0,len(codes),2))
    return data
def decode(data):
    assert len(data)%BYTES==0
    output=[]
    for start in range(0,len(data),BYTES):
        predictor,index,_=struct.unpack_from('<hBB',data,start)
        assert index<=88
        output.append(predictor)
        for i in range(SAMPLES-1):
            code=(data[start+4+i//2]>>(4*(i&1)))&15
            delta,index=transition(index,code)
            predictor=max(-32768,min(32767,predictor+delta)); output.append(predictor)
    return output
