/* Mode 8/9 completion, timestamp-wrap comparison and ES end notification.
 * Addresses are FR CPU-local. Preserve transient writes and original widths.
 * No card, key selection or descrambler operations. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a) (*(volatile u8 *)(a))
#define S32(o) R32(R32(0xa2414)+(o))
#define S8(o) R8(R32(0xa2414)+(o))

u32 __attribute__((section(".compare_handler"))) tr_raw_wrap_compare(u32 left,u32 right)
{
    if ((left>>29)==7 && (right>>29)==0) return 0;
    if ((left>>29)==0 && (right>>29)==7) return 1;
    return left>=right;
}

u32 __attribute__((section(".completion_handler"))) tr_raw_completion(u32 pending)
{
    u32 events=pending,flag;
    if (pending&0x08000000u) {
        if (R8(0xa269d)==1) {
            R8(0xa265c)=3; R8(0xa265d)=1;
            R32(0xa2664)+=S8(0x18);
            if (R32(0xa2664)>=S32(0x20)) R32(0xa2664)=0;
            flag=R32(0x13884); R32(0xa2670)=flag;
            R32(0xa2674)=R32(0x13898); R32(0xa2678)=R32(0x1389c);
            R32(0xa267c)=R32(0x138a4); R32(0xa2680)=R32(0x138a8);
            R32(0xa2684)=R32(0x138ac); R32(0xa2688)=R32(0x138b0);
            R32(0xa268c)=R32(0x138b4); R32(0xa2690)=R32(0x138b8);
            R32(0xa2694)=R32(0x138bc); R32(0xa2698)=R32(0x138d8);
            R8(0xa265c)=R32(0xa2660)==R32(0xa2664)?4:1;
            if (flag&0x10000u) R8(0xa265c)=5;
        } else if (R8(0xa269d)==2) {
            R8(0xa2634)=3; R8(0xa2635)=1;
            R32(0xa263c)+=S8(0x19);
            if (R32(0xa263c)>=S32(0x28)) R32(0xa263c)=0;
            flag=R32(0x13884); R32(0xa2648)=flag;
            R32(0xa264c)=R32(0x13894); R32(0xa2650)=R32(0x13898);
            R32(0xa2654)=R32(0x1389c); R32(0xa2658)=R32(0x138a4);
            R8(0xa2634)=R32(0xa2638)==R32(0xa263c)?4:1;
            if (flag&0x10000u) R8(0xa2634)=5;
        }
        R8(0xa269d)=0;
        events=(events&0xf7ffffffu)|0x30000u;
    }
    if (pending&0x04000000u) {
        if (R8(0xa269c)==3) {
            R8(0xa25a4)=3; R8(0xa25a6)=1;
            R32(0xa25ac)+=S8(0x1a);
            if (R32(0xa25ac)>=S32(0x30)) R32(0xa25ac)=0;
            R32(0xa25b0)=R32(0x13900); R32(0xa25b4)=R32(0x13904);
            R32(0xa25b8)=R32(0x13908); R8(0xa25bc)=R8(0x1390c);
            R8(0xa25a4)=R32(0xa25a8)==R32(0xa25ac)?4:1;
        } else if (R8(0xa269c)==4) {
            R8(0xa2574)=3; R8(0xa2575)=1;
            R32(0xa257c)+=S8(0x1b);
            if (R32(0xa257c)>=S32(0x50)) R32(0xa257c)=0;
            /* Original publishes the ready flag twice, before metadata. */
            R8(0xa2575)=1;
            flag=R32(0x13904); R32(0xa2588)=flag;
            R32(0xa2598)=R32(0x13908); R32(0xa259c)=R32(0x1390c);
            R32(0xa2590)=R32(0x13910); R32(0xa258c)=R32(0x13914);
            R32(0xa2594)=R32(0x13918); R32(0xa25a0)=R32(0x1391c);
            R8(0xa2574)=R32(0xa2578)==R32(0xa257c)?4:1;
            if (flag&0x10000u) R8(0xa2574)=5;
        }
        R8(0xa269c)=0;
        events=(events&0xfbffffffu)|0x30000u;
    }
    return events;
}

u32 __attribute__((section(".end_handler"))) tr_raw_es_end(u32 stream)
{
    u32 offset=(stream&0xffffu)*0x4cu;
    if (!R8(0xa275d+offset) || !(R8(R32(0xa2418))&0x30u) ||
        !R16(0xa2916) || R8(0xa2823)==3) {
        R8(0xa2820+stream)=3; R8(0xa2732+offset)=0;
    } else R8(0xa2820+stream)=0;
    R8(0xa275d+offset)=0; R8(0xa2735+offset)=1;
    R32(0xa284c)+=1; R32(0xa3918)=R32(0xa284c);
    return 0x80000u;
}
