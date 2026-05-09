#ifndef IXLAND_PRESENTER_BRIDGE_H
#define IXLAND_PRESENTER_BRIDGE_H

#import <UIKit/UIKit.h>

UIViewController *active_presenter(void);
void set_active_presenter(UIViewController *controller);
void clear_active_presenter(UIViewController *controller);

#endif
