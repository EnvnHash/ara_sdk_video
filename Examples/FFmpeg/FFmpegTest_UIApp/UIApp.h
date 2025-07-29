//
// Created by user on 24.11.2020.
//

#pragma once

#include <UIApplication.h>

namespace ara {

class UIApp : public UIApplication {
public:
    UIApp();
    void init(std::function<void(UINode&)> initCb) override;
    void exit() override;

private:
};

}
