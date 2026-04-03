//
//  AppGroup.m
//  iSH
//
//  Created by Theodore Dubois on 2/28/20.
//

#import "AppGroup.h"
#import <Foundation/Foundation.h>

static NSString *CurrentAppGroupIdentifier(void) {
    static NSString *appGroupIdentifier;
    if (appGroupIdentifier != nil)
        return appGroupIdentifier;

    NSString *configuredAppGroup = [NSBundle.mainBundle objectForInfoDictionaryKey:@"ISHAppGroupIdentifier"];
    if ([configuredAppGroup isKindOfClass:NSString.class] && configuredAppGroup.length > 0)
        return appGroupIdentifier = configuredAppGroup;

    NSDictionary *extension = [NSBundle.mainBundle objectForInfoDictionaryKey:@"NSExtension"];
    NSString *extensionAppGroup = extension[@"NSExtensionFileProviderDocumentGroup"];
    if (![extensionAppGroup isKindOfClass:NSString.class] || extensionAppGroup.length == 0)
        return nil;

    return appGroupIdentifier = extensionAppGroup;
}

NSArray<NSString *> *CurrentAppGroups(void) {
    NSString *appGroupIdentifier = CurrentAppGroupIdentifier();
    if (appGroupIdentifier == nil)
        return nil;
    return @[appGroupIdentifier];
}

NSURL *ContainerURL(void) {
    NSString *appGroup = CurrentAppGroups()[0];
    return [NSFileManager.defaultManager containerURLForSecurityApplicationGroupIdentifier:appGroup];
}
