#import "presenter_bridge.h"

static __weak UIViewController *gActivePresenter;

UIViewController *active_presenter(void)
{
    return gActivePresenter;
}

void set_active_presenter(UIViewController *controller)
{
    gActivePresenter = controller;
}

void clear_active_presenter(UIViewController *controller)
{
    if (gActivePresenter == controller)
        gActivePresenter = nil;
}
