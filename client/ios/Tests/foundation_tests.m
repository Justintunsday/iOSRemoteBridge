#import "../RemoteBridge/IRBProtocol.h"
#import "../RemoteBridge/IRBProfile.h"
#import "../RemoteBridge/IRBVideoDecoder.h"
#include <stdio.h>
#include <stdlib.h>
static void require(BOOL ok) {if(!ok) {fprintf(stderr,"Objective-C controller test failed\n");exit(1);}}
int main(void) { @autoreleasepool {
    NSDictionary *object=@{@"host":@"127.0.0.1",@"port":@7443,@"token":[@"a" stringByPaddingToLength:64 withString:@"a" startingAtIndex:0],@"certificate_sha256":[@"b" stringByPaddingToLength:64 withString:@"b" startingAtIndex:0]};
    NSData *(^json)(id)=^NSData *(id value){return [NSJSONSerialization dataWithJSONObject:value options:0 error:NULL];};
    IRBProfile *profile=[IRBProfile fromData:json(object) error:NULL];require(profile&&profile.pinBytes.length==32&&profile.port==7443);
    for(id bad in @[@YES,@0,@65536,@7443.5,@"7443"]) {NSMutableDictionary *changed=[object mutableCopy];changed[@"port"]=bad;require(![IRBProfile fromData:json(changed) error:NULL]);}
    require(![IRBProfile fromData:json(@[]) error:NULL]);require(![profile withHost:@"host with spaces" port:@"7443" error:NULL]);
    require([profile withHost:@"192.168.1.2" port:@"7443" error:NULL]!=nil);require([IRBProfile fromData:profile.JSONData error:NULL]!=nil);
    IRBPacketReader *reader=[IRBPacketReader new];NSData *response=IRBClientPacket(IRB_RESPONSE,1,0,json(@{@"event":@"authorized"}));NSUInteger count=0;
    for(NSUInteger i=0;i<response.length;++i) count+=[reader append:[response subdataWithRange:NSMakeRange(i,1)]].count;require(count==1&&!reader.error);
    reader=[IRBPacketReader new];require([reader append:IRBClientPacket(IRB_RESPONSE,2,0,json(@{}))].count==0&&reader.error!=nil);
    reader=[IRBPacketReader new];require([reader append:IRBClientPacket(IRB_HID,1,0,json(@{}))].count==0&&reader.error!=nil);
    uint8_t oversized[IRB_HEADER_SIZE];irb_header header={IRB_VIDEO,0,IRB_MAX_PAYLOAD+1,1,1};irb_pack(oversized,header);
    reader=[IRBPacketReader new];require([reader append:[NSData dataWithBytes:oversized length:sizeof(oversized)]].count==0&&reader.error!=nil);
    const uint8_t annex[]={0,0,0,1,0x67,0x22,0,0,1,0x68,0x33,0,0,0,1,0x65,0x44};
    NSArray *nals=IRBSplitAnnexB([NSData dataWithBytes:annex length:sizeof(annex)]);require(nals.count==3&&[nals[0] length]==2&&[nals[2] length]==2);
    require(IRBSplitAnnexB([@"invalid" dataUsingEncoding:NSUTF8StringEncoding]).count==0);
    puts("Production Objective-C profile, framing, hostile argument and Annex-B checks passed");
}return 0;}
