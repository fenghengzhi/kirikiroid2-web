#include "MessageBox.h"
#include "cocos2d/MainScene.h"
#include "ui/UIButton.h"
#include "ui/UIText.h"
#include "ui/UIScrollView.h"
#include "ui/UIHelper.h"
#include "ui/UILoadingBar.h"
#include "2d/CCLabel.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "csd/CsdUIFactory.h"

using namespace cocos2d;
using namespace cocos2d::ui;

void TVPMessageBoxForm::show(const std::string &caption,
                             const std::string &text, int nBtns,
                             const std::string *btnText,
                             const std::function<void(int)> &callback) {
    auto *ret = new TVPMessageBoxForm;
    ret->autorelease();
    ret->init(caption, text, nBtns, btnText, callback);
    TVPMainScene::GetInstance()->pushUIForm(ret, TVPMainScene::eEnterAniNone);
    ret->rearrangeLayout();
}

void TVPMessageBoxForm::rearrangeLayout() {
    cocos2d::Size sceneSize = TVPMainScene::GetInstance()->getUINodeSize();
    setContentSize(sceneSize);
    if (RootNode) {
        cocos2d::Size rootSize = RootNode->getContentSize();
        float s = std::min(sceneSize.width / rootSize.width,
                           sceneSize.height / rootSize.height);
        RootNode->setScale(s);
        RootNode->setAnchorPoint(Vec2(0.5f, 0.5f));
        RootNode->setPosition(Vec2(sceneSize.width / 2, sceneSize.height / 2));
    }
}

void TVPMessageBoxForm::showYesNo(const std::string &caption,
                                  const std::string &text,
                                  const std::function<void(int)> &callback) {
    LocaleConfigManager *mgr = LocaleConfigManager::GetInstance();
    std::string btns[2] = { mgr->GetText("msgbox_yes"),
                            mgr->GetText("msgbox_no") };
    return show(caption, text, 2, btns, callback);
}

void TVPMessageBoxForm::init(const std::string &caption,
                             const std::string &text, int nBtns,
                             const std::string *btnText,
                             const std::function<void(int)> &callback) {
    _callback = callback;

    initFromFile(nullptr, Csd::createMessageBox(), nullptr);

    if(_title)
        _title->setString(caption);
    if(_textContent) {
        float textWidth = _textContainer->getContentSize().width - 40;
        _textContent->ignoreContentAdaptWithSize(true);
        auto *label = dynamic_cast<Label *>(_textContent->getVirtualRenderer());
        if (label)
            label->setMaxLineWidth(textWidth);
        _textContent->setString(text);

        const cocos2d::Size textSize = _textContent->getContentSize();
        const cocos2d::Size viewSize = _textContainer->getInnerContainerSize();
        float innerHeight = std::max(viewSize.height, textSize.height + 20);
        _textContainer->setInnerContainerSize(Size(viewSize.width, innerHeight));
        _textContent->setPosition(Vec2(20, innerHeight));
    }
    _btnModel->retain();
    _btnModel->removeFromParentAndCleanup(false);

    float containerWidth = _btnList->getContentSize().width;
    float containerHeight = _btnList->getContentSize().height;
    float btnGap = 8.0f;
    float edgePad = 10.0f;
    float availWidth = containerWidth - edgePad * 2 - btnGap * std::max(0, nBtns - 1);

    std::string fontName = _btnBody->getTitleFontName();
    cocos2d::Color3B fontColor = _btnBody->getTitleColor();
    float fontSize = std::min(_btnBody->getTitleFontSize(), 48.0f);

    std::vector<Button *> buttons;
    float totalWidth = 0;

    for (int i = 0; i < nBtns; ++i) {
        auto *btn = Button::create("img/empty.png", "img/gray.png", "img/gray.png");
        btn->setScale9Enabled(true);
        btn->setTitleText(btnText[i]);
        btn->setTitleFontName(fontName);
        btn->setTitleFontSize(fontSize);
        btn->setTitleColor(fontColor);
        btn->setZoomScale(0.05f);

        float textW = btn->getTitleRenderer()->getContentSize().width;
        float btnWidth = textW + fontSize;
        btn->setContentSize(Size(btnWidth, containerHeight - 16));

        totalWidth += btnWidth;
        buttons.push_back(btn);
    }

    if (totalWidth > availWidth && totalWidth > 0) {
        float ratio = availWidth / totalWidth;
        fontSize = std::max(fontSize * ratio, 24.0f);
        totalWidth = 0;
        for (int i = 0; i < nBtns; ++i) {
            buttons[i]->setTitleFontSize(fontSize);
            float textW = buttons[i]->getTitleRenderer()->getContentSize().width;
            float btnWidth = textW + fontSize;
            buttons[i]->setContentSize(Size(btnWidth, containerHeight - 16));
            totalWidth += btnWidth;
        }
    }

    for (int i = 0; i < nBtns; ++i) {
        buttons[i]->addClickEventListener([this, i](Ref *) {
            retain();
            TVPMainScene::GetInstance()->popUIForm(this, TVPMainScene::eLeaveAniNone);
            if (_callback)
                _callback(i);
            release();
        });
        _btnList->addChild(buttons[i]);
        buttons[i]->setTag(i);
    }

    float gap = (containerWidth - totalWidth) / (nBtns + 1);
    float x = gap;
    for (auto *btn : buttons) {
        btn->setAnchorPoint(Vec2(0, 0.5f));
        btn->setPosition(Vec2(x, containerHeight / 2));
        x += btn->getContentSize().width + gap;
    }

    _btnModel->release();
    _btnModel = nullptr;
}

void TVPMessageBoxForm::bindBodyController(const Node *allNodes) {
    _title = allNodes->getChildByName<Text *>("title");
    _textContent = allNodes->getChildByName<Text *>("content");
    _textContainer = allNodes->getChildByName<ScrollView *>("text");

    _btnBody = allNodes->getChildByName<Button *>("btnBody");
    _btnModel = allNodes->getChildByName<Widget *>("btn");
    _btnList = _btnModel->getParent();
}

void TVPMessageBoxForm::onKeyPressed(cocos2d::EventKeyboard::KeyCode keyCode,
                                     cocos2d::Event *event) {
    if(keyCode == cocos2d::EventKeyboard::KeyCode::KEY_BACK) {
        TVPMainScene::GetInstance()->popUIForm(this,
                                               TVPMainScene::eLeaveAniNone);
    }
}

TVPSimpleProgressForm *TVPSimpleProgressForm::create() {
    auto *form = new TVPSimpleProgressForm;
    form->autorelease();
    form->initFromFile(Csd::createProgressBox());
    return form;
}

void TVPSimpleProgressForm::initButtons(
    const std::vector<
        std::pair<std::string, std::function<void(cocos2d::Ref *)>>> &vec) {
    cocos2d::Size btnSize = _btnCell->getContentSize();
    float totalWidth = 0;
    float containerWidth = _btnContainer->getContentSize().width;
    float edge = _btnCell->getPosition().x;
    containerWidth -= edge * 2;
    std::vector<Node *> buttons;
    float btnEdge =
        btnSize.width - _btnButton->getTitleRenderer()->getContentSize().width;
    for(const auto &it : vec) {
        _btnButton->setTitleText(it.first);
        _btnButton->addClickEventListener(it.second);
        btnSize.width =
            _btnButton->getTitleRenderer()->getContentSize().width + btnEdge;
        _btnCell->setContentSize(btnSize);
        ui::Helper::doLayout(_btnCell);
        totalWidth += btnSize.width;
        Widget *btn = _btnCell->clone();
        buttons.push_back(btn);
        _btnContainer->addChild(btn);
    }
    float x = edge;
    float gap = (containerWidth - totalWidth) / buttons.size();
    for(Node *btn : buttons) {
        btn->setPositionX(x);
        x += btn->getContentSize().width;
    }
}

void TVPSimpleProgressForm::setTitle(const std::string &text) {
    _textTitle->setString(text);
}
void TVPSimpleProgressForm::setContent(const std::string &text) {
    _textContent->setString(text);
}

void TVPSimpleProgressForm::setPercentWithText(float percent) {
    _progressBar[0]->setPercent(percent);
    char tmp[16];
    sprintf(tmp, "%2.2f%%", percent);
    _textProgress[0]->setString(tmp);
}
void TVPSimpleProgressForm::setPercentWithText2(float percent) {
    _progressBar[1]->setPercent(percent);
    char tmp[16];
    sprintf(tmp, "%2.2f%%", percent);
    _textProgress[1]->setString(tmp);
}

void TVPSimpleProgressForm::setPercentOnly(float percent) {
    _progressBar[0]->setPercent(percent * 100);
}

void TVPSimpleProgressForm::setPercentOnly2(float percent) {
    _progressBar[1]->setPercent(percent * 100);
}

void TVPSimpleProgressForm::setPercentText(const std::string &text) {
    _textProgress[0]->setString(text);
}

void TVPSimpleProgressForm::setPercentText2(const std::string &text) {
    _textProgress[1]->setString(text);
}

void TVPSimpleProgressForm::setProgress2Visible(bool visible) {
    // TODO
}

void TVPSimpleProgressForm::bindBodyController(const Node *allNodes) {
    _progressBar[0] = allNodes->getChildByName<LoadingBar *>("progrss_1");
    _progressBar[1] = allNodes->getChildByName<LoadingBar *>("progrss_2");
    _textProgress[0] = allNodes->getChildByName<Text *>("progress_text_1");
    _textProgress[1] = allNodes->getChildByName<Text *>("progress_text_2");
    _textContent = allNodes->getChildByName<Text *>("text");
    _textTitle = allNodes->getChildByName<Text *>("title");
    _btnContainer = allNodes->getChildByName("btnList");
    _btnCell = allNodes->getChildByName<Widget *>("btnCell");
    _btnButton = allNodes->getChildByName<Button *>("btn");
    _btnCell->removeFromParentAndCleanup(false);
}
