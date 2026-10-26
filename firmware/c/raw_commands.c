/* Mode 8/9 command/status handler a0000 and transfer starter a0b0e.
 * No B-CAS/key/descrambling operations. All addresses are FR CPU-local.
 * Match original write widths/order, command FIFO ownership and priorities. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a) (*(volatile u8 *)(a))
#define S32(o) R32(R32(0xa2414)+(o))
#define S8(o) R8(R32(0xa2414)+(o))
#define M16(o) R16(R32(0xa2410)+(o))
#define M8(o) R8(R32(0xa2410)+(o))
#define GENERIC_PID() (R16(R32(0xa2418)+0x24)&0x1fffu)
#define CALL0(a) (((void (*)(void))(a))())

static const u8 status_counters[] = {0x00,0x24,0x04,0x28,0x38,0x3c,0x08,0x2c};

static void status_reply(void)
{
    u32 reply=R32(0xa240c),i;
    R8(reply)=R8(0xa2420); R8(reply+1)=R8(0xa2850);
    for (i=0;i<4;i++) R8(reply+4+i)=R8(0xa2820+i);
    R8(reply+8)=R8(0xa25f4); R8(reply+9)=R8(0xa25f5);
    R8(reply+10)=R8(0xa25a5); R8(reply+11)=R8(0xa2826);
    R16(reply+12)=R16(0x3000); R16(reply+14)=R16(0x3004);
    for (i=0;i<8;i++) R16(reply+16+i*2)=R32(0xa3900+status_counters[i]);
    for (i=0;i<4;i++) R32(reply+32+i*4)=R32(0xa27f8+i*4)&0x3fffffffu;
    R32(reply+48)=R32(0xa2808);
    for (i=0;i<4;i++) R8(reply+52+i)=R32(0xa2564+i*4);
    R32(reply+56)=0; R32(reply+60)=0;
    R16(reply+2)=(R32(R16(0x10078)+R32(0xa2600)+4)>>8)&0x1fffu;
}

static u32 metadata(u32 events)
{
    u32 command=R32(0xa2410),type=(R8(command+1)>>1)&7u;
    u32 next_video=(R32(0xa2564)+1)&15u,next_clock=(R32(0xa256c)+1)&15u;
    u32 dst;
    if (type && (next_video==R32(0xa2568) || next_clock==R32(0xa2570))) {
        if (R8(0xa25f5)!=4) { events|=0x20000u; R8(0xa25f5)=5; }
        return events;
    }
    if (!(R8(command+1)&1u)) {
        if ((R8(command)&0x10u) && R8(0xa25a5)==5) {
            R8(0xa25a4)=0; R8(0xa25a5)=0;
            R16(0xa2612)=M16(10); R16(0xa2916)=M16(10); R16(0xa2918)=M16(10);
            if (R16(0xa2864)==R16(0xa2918)) R8(0xa291b)=R8(0xa2717);
            R8(0xa2620)=1; R32(0xa2630)=1;
        }
        if (R8(command)&8u) {
            if (R16(0xa2864) && R8(0xa2820)==2) {
                R8(0xa2820)=0;
                if (!GENERIC_PID()) R8(0xa275d)=1;
                events|=0x20000u;
            }
            if (R16(0xa2934) && R8(0xa2822)==2) {
                R8(0xa2822)=0;
                if (!GENERIC_PID()) R8(0xa27f5)=1;
                events|=0x20000u;
            }
        }
        if ((R8(command)&4u) && R16(0xa28a0) && R8(0xa2821)==2) {
            R8(0xa2821)=0;
            if (!GENERIC_PID()) R8(0xa27a9)=1;
            events|=0x20000u;
        }
        if ((R8(command)&2u) && !R16(0xa2864)) {
            /* Original writes active PID before its per-stream copy. */
            R16(0xa2864)=M16(16); R16(0xa2714)=R16(0xa2864);
            if (R8(0xa2820)==2) {
                R8(0xa265c)=0; R8(0xa2820)=0;
                if (!GENERIC_PID()) R8(0xa275d)=1;
            }
        }
        if ((R8(command)&1u) && !R16(0xa28a0)) {
            R16(0xa28a0)=M16(18); R16(0xa2760)=R16(0xa28a0);
            if (R8(0xa2821)==2) {
                R8(0xa2634)=0; R8(0xa2821)=0;
                if (!GENERIC_PID()) R8(0xa27a9)=1;
            }
        }
    }
    if (type==1 && !R8(0xa28b5)) type=0;
    /* The original keeps type 2 but takes the direct path when disabled. */
    if (type==0 || (type==2 && !R8(0xa28cc))) {
        if ((R8(command)&0x80u) && R16(0xa28be)!=M16(4)) {
            R16(0xa2618)=M16(4); R8(0xa28b8)=1;
        }
        if ((R8(command)&0x40u) && (R16(0xa28ce)!=M16(6) || R16(0xa28be)!=M16(4))) {
            R16(0xa2610)=M16(6); R16(0xa261a)=M16(4);
        }
        if (R8(command)&0x10u) R16(0xa2612)=M16(10);
        if (M8(0)&8u) {
            R16(0xa261c)=M16(12);
            if (!R8(0xa2898)) R32(0xa2894)=M16(12);
            else R16(0xa2614)=M16(12);
            if (R32(0xa2894)!=M16(12)) R8(0xa28d4)=1;
        }
        if (M8(0)&4u) {
            R16(0xa261e)=M16(14);
            if (!R8(0xa28ae)) R16(0xa28ac)=M16(14);
            else R16(0xa2616)=M16(14);
            if (R16(0xa28ac)!=M16(14)) R8(0xa28d4)=1;
        }
    } else if (type==1) {
        dst=0xa2424+R32(0xa2564)*8;
        R8(dst)=R8(command); R8(dst+1)=R8(command+1);
        R16(dst+4)=M16(4); R16(dst+6)=M16(6);
        R32(0xa2564)=next_video; events|=0x20000u;
    } else if (type==2) {
        dst=0xa24a4+R32(0xa256c)*12;
        R8(dst)=M8(0); R8(dst+1)=M8(1);
        R16(dst+6)=M16(10); R16(dst+8)=M16(12); R16(dst+10)=M16(14);
        R32(0xa256c)=next_clock; events|=0x20000u;
    }
    R8(command)=0; R8(R32(0xa2410)+1)=0; R16(0xc0)=0x100;
    return events&~0x80u;
}

static u32 start_queue(u32 queue,u32 counter,u32 step,u32 limit,u32 events)
{
    u32 next;
    if (R8(queue)!=5) {
        if (R8(queue)==0 || R8(queue)==4) R8(queue)=1;
        next=R32(counter)+S8(step);
        if (next>=S32(limit)) next=0;
        R32(queue+4)=next; events|=0x10000u;
    }
    return events;
}

static void acknowledge(u32 counter,u32 saved,u32 notification)
{
    if (R8(0xa2420)==3 || R8(0xa2420)==4) {
        R32(saved)=R32(counter); R16(0xc0)=notification;
    }
    if (R8(0xa2420)==1) R8(0xa2420)=2;
}

u32 __attribute__((section(".command_handler"))) tr_raw_commands(u32 pending)
{
    u32 events=pending,low,upper;
    if (pending&0x4000u) { status_reply(); events&=~0x4000u; }
    if (pending&0x20u) {
        CALL0(0xa126c); CALL0(0xa1394); CALL0(0xa2060);
        R8(0xa2420)=1; R32(0xa2400)=0; R32(0xa2404)=0xfc000004u;
        R16(0xc0)=0x40;
        CALL0(0xa0eb6); CALL0(0xa12d4); CALL0(0xa1520);
        return 0;
    }
    if (pending&1u) { CALL0(0xa09c8); R16(0xc0)=2; events&=~1u; }
    if (pending&0x80u) events=metadata(events);
    if (pending&2u) {
        events=start_queue(0xa265c,0xa3900,0x18,0x20,events);
        acknowledge(0xa3900,0xa3924,4); events&=~2u;
    }
    if (pending&4u) {
        events=start_queue(0xa2634,0xa3904,0x19,0x28,events);
        acknowledge(0xa3904,0xa3928,8); events&=~4u;
    }
    if (pending&8u) {
        if (R8(0xa25f4)==0) {
            R8(0xa25f4)=1; R32(0xa2404)|=0x20;
            R32(0x40)|=0x20; R8(0xa25f6)=1;
            R32(0x1078)=R32(0xa2608); R32(0x1068)=0x1000000;
        }
        if (R8(0xa2420)==1) R8(0xa2420)=2;
        if (R8(0xa390c)&1u) events|=0x20000u;
        events&=~8u;
    }
    if (pending&0x10u) {
        events=start_queue(0xa25a4,0xa3908,0x1a,0x30,events);
        if (R8(0xa25f5)!=4) {
            upper=R32(0xa3940); low=upper&0xfffu; upper&=0xfffff000u;
            R32(0xa2624)=upper;
            if (R8(0xa25f5)==0) R8(0xa25f5)=1;
            if (R8(0xa25f5)==3 && !low) R8(0xa25f5)=1;
            if (R8(0xa25f5)==1 && (R8(0xa25a5)==2 || R8(0xa25a5)==3) &&
                !((u32 (*)(u32,u32))0xe420a)(R32(0xa2628),upper)) {
                R8(0xa25f5)=2; events|=0x20000u;
            }
            if (low>R32(0xa2630)) { R8(0xa25f5)=3; events|=0x20000u; }
        }
        acknowledge(0xa3908,0xa392c,0x10); events&=~0x10u;
    }
    if (pending&0x40u) {
        events=start_queue(0xa2574,0xa3938,0x1b,0x50,events);
        acknowledge(0xa3938,0xa393c,0x80); events&=~0x40u;
    }
    return events;
}

static u32 ready(u32 state,u32 flag)
{
    return R8(state)==1 && R8(flag)==0;
}

u32 __attribute__((section(".transfer_handler"))) tr_raw_start_transfers(u32 pending)
{
    if (!(pending&0x10000u)) return pending;
    if (ready(0xa265c,0xa265d) || ready(0xa2634,0xa2635) ||
        ready(0xa25a4,0xa25a6) || ready(0xa2574,0xa2575)) {
        if (R8(0xa269d)==0) {
            if (ready(0xa265c,0xa265d)) {
                R8(0xa265c)=2; R8(0xa269d)=1;
                R32(0x1080)=S32(0x1c)&0x07ffffe0u;
                R32(0x10c0)=S8(0x18); R32(0x1090)=R32(0xa2664)&0xffffffu;
            } else if (ready(0xa2634,0xa2635)) {
                R8(0xa2634)=2; R8(0xa269d)=2;
                R32(0x1080)=S32(0x24)&0x07ffffe0u;
                R32(0x10c0)=S8(0x19); R32(0x1090)=R32(0xa263c)&0xffffffu;
            }
            if (R8(0xa269d)!=0) { R32(0x100c0)=0; R32(0x10b0)=0x1000000; }
        }
        if (R8(0xa269c)==0) {
            if (ready(0xa25a4,0xa25a6)) {
                R8(0xa25a4)=2; R8(0xa269c)=3;
                R32(0x1084)=S32(0x2c)&0x07ffffe0u;
                R32(0x10c4)=S8(0x1a); R32(0x1094)=R32(0xa25ac)&0xffffffu;
            } else if (ready(0xa2574,0xa2575)) {
                R8(0xa2574)=2; R8(0xa269c)=4;
                R32(0x1084)=S32(0x4c)&0x07ffffe0u;
                R32(0x10c4)=S8(0x1b); R32(0x1094)=R32(0xa257c)&0xffffffu;
            }
            if (R8(0xa269c)!=0) { R32(0x100c4)=0; R32(0x10b4)=0x1000000; }
        }
    }
    return pending&~0x10000u;
}
