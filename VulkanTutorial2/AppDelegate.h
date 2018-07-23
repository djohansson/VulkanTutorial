//
//  AppDelegate.h
//  VulkanTutorial2
//
//  Created by Daniel Johansson on 2018-07-20.
//  Copyright © 2018 Ubisoft. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface AppDelegate : NSObject <NSApplicationDelegate>

@property (nonatomic, strong) NSDisplayLink1* timer;
@property (nonatomic, assign) NSTimeInterval lastTimestamp;


@end

