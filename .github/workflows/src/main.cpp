#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/UILayer.hpp>
#include <deque>
#include <cmath>

using namespace geode::prelude;

static constexpr int kStageTicks[] = {0, 2, 4, 8, 16, 24};
static constexpr int kStageCount = 6;

static int currentStage() {
    int s = Mod::get()->getSavedValue<int>("stage", 0);
    int maxS = (int)Mod::get()->getSettingValue<int64_t>("max-stage");
    if (s < 0) s = 0;
    if (s > maxS) s = maxS;
    return s;
}
static void setStage(int s) { Mod::get()->setSavedValue<int>("stage", s); }
static int nextStage() {
    int maxS = (int)Mod::get()->getSettingValue<int64_t>("max-stage");
    int s = currentStage() + 1;
    if (s > maxS || s >= kStageCount) s = 0;
    setStage(s);
    return s;
}

namespace Predictor {
    constexpr float kTickDt      = 1.f / 240.f;
    constexpr float kBaseSpeed   = 311.58f;
    constexpr float kJumpVel     = 11.180032f;
    constexpr float kGravity     = 0.958199f;
    constexpr int   kHorizon     = 90;

    bool diesWithDelay(PlayerObject* p, std::vector<GameObject*> const& hazards, int delay) {
        CCRect base = p->getObjectRect();
        float x = 0.f, y = 0.f;
        float vy = 0.f;
        bool jumped = false;
        float speed = kBaseSpeed * (p->m_playerSpeed > 0.f ? p->m_playerSpeed : 1.f);
        float flip = p->m_isUpsideDown ? -1.f : 1.f;

        for (int t = 0; t < kHorizon; ++t) {
            x += speed * kTickDt;
            if (t == delay) { vy = kJumpVel * flip; jumped = true; }
            if (jumped) {
                y += vy * 60.f * kTickDt;
                vy -= kGravity * flip * 60.f * kTickDt;
                if ((flip > 0 && y <= 0.f) || (flip < 0 && y >= 0.f)) { y = 0.f; jumped = false; vy = 0.f; }
            }
            CCRect r(base.origin.x + x, base.origin.y + y, base.size.width, base.size.height);
            for (auto* h : hazards) {
                if (r.intersectsRect(h->getObjectRect())) return true;
            }
        }
        return false;
    }

    int pickDelay(GJBaseGameLayer* gl, PlayerObject* p, int maxDelay) {
        if (!p || !p->m_isOnGround) return 0;
        if (p->m_isShip || p->m_isBall || p->m_isBird || p->m_isDart ||
            p->m_isRobot || p->m_isSpider || p->m_isSwing) return 0;

        float reach = kBaseSpeed * (p->m_playerSpeed > 0.f ? p->m_playerSpeed : 1.f)
                    * (kHorizon * kTickDt) + 60.f;
        float px = p->getPositionX();

        std::vector<GameObject*> hazards;
        if (gl->m_objects) {
            for (auto* obj : CCArrayExt<GameObject*>(gl->m_objects)) {
                if (!obj || obj->m_objectType != GameObjectType::Hazard) continue;
                float ox = obj->getPositionX();
                if (ox < px - 30.f || ox > px + reach) continue;
                hazards.push_back(obj);
            }
        }
        if (hazards.empty()) return 0;

        if (!diesWithDelay(p, hazards, 0)) return 0;
        for (int d = 1; d <= maxDelay; ++d) {
            if (!diesWithDelay(p, hazards, d)) return d;
        }
        return 0;
    }
}

struct Pending { int dueTick; bool down; int button; bool p1; };

class $modify(CABaseGameLayer, GJBaseGameLayer) {
    struct Fields {
        std::deque<Pending> queue;
        int lastTick = 0;
        bool replaying = false;
    };

    void handleButton(bool down, int button, bool isPlayer1) {
        auto f = m_fields.self();
        int stage = currentStage();

        if (f->replaying || !down || stage == 0 || !PlayLayer::get()) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto* p = isPlayer1 ? m_player1 : m_player2;
        int d = Predictor::pickDelay(this, p, kStageTicks[stage]);
        if (d <= 0) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        f->queue.push_back({ m_gameState.m_currentProgress + d, true, button, isPlayer1 });
    }

    void processCommands(float dt) {
        auto f = m_fields.self();
        int now = m_gameState.m_currentProgress;

        if (now < f->lastTick || (m_player1 && m_player1->m_isDead)) f->queue.clear();
        f->lastTick = now;

        while (!f->queue.empty() && f->queue.front().dueTick <= now) {
            auto e = f->queue.front();
            f->queue.pop_front();
            f->replaying = true;
            GJBaseGameLayer::handleButton(e.down, e.button, e.p1);
            f->replaying = false;
        }
        GJBaseGameLayer::processCommands(dt);
    }
};

static std::string stageLabel(int s) {
    return s == 0 ? "CA: off" : fmt::format("CA: {}t", kStageTicks[s]);
}

class $modify(CAUILayer, UILayer) {
    bool init(GJBaseGameLayer* gl) {
        if (!UILayer::init(gl)) return false;
        if (!Mod::get()->getSettingValue<bool>("show-button")) return true;

        auto win = CCDirector::get()->getWinSize();
        auto spr = ButtonSprite::create(stageLabel(currentStage()).c_str(), "bigFont.fnt", "GJ_button_04.png", .6f);
        spr->setScale(.55f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [spr](CCMenuItemSpriteExtra*) {
            int s = nextStage();
            spr->setString(stageLabel(s).c_str());
        });
        btn->setOpacity(150);

        auto menu = CCMenu::create();
        menu->setID("clickassist-menu"_spr);
        menu->setPosition({ win.width - 55.f, win.height - 18.f });
        menu->addChild(btn);
        this->addChild(menu, 100);
        return true;
    }
};
