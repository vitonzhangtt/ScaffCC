// RUN: %clang_cc1 -triple x86_64-apple-darwin11 -g -S %s -o -
@class Bar;
@interface Foo
@property (strong, nonatomic) Bar *window;
@end

@interface Foo__ : Foo
@end
@implementation Foo__
@end

@implementation Foo
@synthesize window = _window;
@end

