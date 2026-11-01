#import "IRBProfile.h"
#import <Security/Security.h>
static NSError *profileError(NSString *message) { return [NSError errorWithDomain:@"IRBProfile" code:1 userInfo:@{NSLocalizedDescriptionKey:message}]; }
static BOOL hex64(id value) {
    return [value isKindOfClass:NSString.class]&&[value length]==64&&
        [value rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"] invertedSet]].location==NSNotFound;
}
@implementation IRBProfile
+ (instancetype)fromData:(NSData *)data error:(NSError **)error {
    if(data.length>65536) { if(error) *error=profileError(@"配对文件过大");return nil; }
    id value=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    if(![value isKindOfClass:NSDictionary.class]) { if(error) *error=profileError(@"请选择 client-config.json 配对文件");return nil; }
    NSString *host=value[@"host"];id port=value[@"port"]?:@7443;
    BOOL number=[port isKindOfClass:NSNumber.class]&&CFGetTypeID((__bridge CFTypeRef)port)!=CFBooleanGetTypeID();
    BOOL validHost=[host isKindOfClass:NSString.class]&&host.length>0&&host.length<=253&&
        [host rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:"] invertedSet]].location==NSNotFound;
    if(!validHost||!number||[port doubleValue]!=[port intValue]||[port intValue]<1024||[port intValue]>65535||
       !hex64(value[@"token"])||!hex64(value[@"certificate_sha256"])) {
        if(error) *error=profileError(@"配对文件的地址、端口或密钥格式不正确");return nil;
    }
    IRBProfile *profile=[IRBProfile new];profile->_host=[host copy];profile->_port=[port unsignedShortValue];
    profile->_token=[value[@"token"] copy];profile->_certificatePin=[value[@"certificate_sha256"] lowercaseString];
    NSMutableData *pin=[NSMutableData dataWithLength:32];uint8_t *bytes=pin.mutableBytes;
    for(unsigned i=0;i<32;++i) { unsigned parsed=0;[[NSScanner scannerWithString:[profile->_certificatePin substringWithRange:NSMakeRange(i*2,2)]] scanHexInt:&parsed];bytes[i]=(uint8_t)parsed; }
    profile->_pinBytes=pin;return profile;
}
- (NSData *)JSONData {
    return [NSJSONSerialization dataWithJSONObject:@{@"host":_host,@"port":@(_port),@"token":_token,@"certificate_sha256":_certificatePin} options:0 error:NULL];
}
- (IRBProfile *)withHost:(NSString *)host port:(NSString *)port error:(NSError **)error {
    NSCharacterSet *nonDigits=[[NSCharacterSet decimalDigitCharacterSet] invertedSet];
    if(!port.length||[port rangeOfCharacterFromSet:nonDigits].location!=NSNotFound) { if(error) *error=profileError(@"请输入有效端口");return nil; }
    NSDictionary *object=@{@"host":[host stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet],@"port":@(port.integerValue),@"token":_token,@"certificate_sha256":_certificatePin};
    return [IRBProfile fromData:[NSJSONSerialization dataWithJSONObject:object options:0 error:NULL] error:error];
}
@end
@implementation IRBProfileStore
+ (NSDictionary *)query { return @{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,(__bridge id)kSecAttrService:@"io.github.justintunsday.iosremotebridge.controller",(__bridge id)kSecAttrAccount:@"paired-device"}; }
+ (IRBProfile *)load {
    NSMutableDictionary *query=[[self query] mutableCopy];query[(__bridge id)kSecReturnData]=@YES;query[(__bridge id)kSecMatchLimit]=(__bridge id)kSecMatchLimitOne;
    CFTypeRef result=NULL;if(SecItemCopyMatching((__bridge CFDictionaryRef)query,&result)!=errSecSuccess) return nil;
    NSData *data=CFBridgingRelease(result);return [IRBProfile fromData:data error:NULL];
}
+ (BOOL)save:(IRBProfile *)profile error:(NSError **)error {
    NSDictionary *values=@{(__bridge id)kSecValueData:profile.JSONData,(__bridge id)kSecAttrAccessible:(__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly};
    OSStatus status=SecItemUpdate((__bridge CFDictionaryRef)[self query],(__bridge CFDictionaryRef)values);
    if(status==errSecItemNotFound) { NSMutableDictionary *add=[[self query] mutableCopy];[add addEntriesFromDictionary:values];status=SecItemAdd((__bridge CFDictionaryRef)add,NULL); }
    if(status!=errSecSuccess&&error) *error=profileError(@"无法将配对信息保存到钥匙串");return status==errSecSuccess;
}
+ (void)remove { SecItemDelete((__bridge CFDictionaryRef)[self query]); }
@end
