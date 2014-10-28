
/* Any copyright in this file is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

#import <UIKit/UIKit.h>
#import "TGKit.h"

@interface ViewController : UIViewController <TGKitDelegate, UIAlertViewDelegate>

@property (nonatomic, strong) IBOutlet UITextField *peerId;
@property (nonatomic, strong) IBOutlet UITextField *messageInput;
@property (nonatomic, strong) IBOutlet UITextView *messageView;
@property (nonatomic, strong) IBOutlet UIButton *sendButton;

@end

