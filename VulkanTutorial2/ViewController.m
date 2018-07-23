//
//  ViewController.m
//  VulkanTutorial2
//
//  Created by Daniel Johansson on 2018-07-20.
//  Copyright © 2018 Ubisoft. All rights reserved.
//

#import "ViewController.h"

#include "RenderView.h"

@implementation ViewController

- (void)dealloc
{
}

- (id)initWithCoder:(NSCoder*)coder
{
    self = [super initWithCoder:coder];
    
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    
    self.view = [[RenderView alloc] initWithFrame:self.view.frame];
}

- (void)setRepresentedObject:(id)representedObject
{
    [super setRepresentedObject:representedObject];

    // Update the view, if already loaded.
}


@end
