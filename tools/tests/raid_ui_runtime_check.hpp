#pragma once
#include "tu80_ui_runtime_check.hpp"
#include "game/ui/FJ_Hud.hpp"
#include "game/ui/FJ_InventoryMenu.hpp"
#include "game/entity/mob/MobEffect.hpp"
#include "game/core/BossEvent.hpp"
#include "game/core/User.hpp"
#include "game/client/render/GameRenderer.hpp"
#include "game/ui/ScreenSizeCalculator.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/generation/MultiPlayerLevel.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "platform/pc/audio/PcSoundBank.hpp"
#include "platform/pc/audio/PcAudioDecoder.hpp"

inline int RunRaidUiRegression()
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const std::string folder=std::getenv("LCE_FUI_DIR");
    gConsoleUIController.SetupFont();
    for(const char* name:{"skinWiiU.fui","skinGraphics.fui","skinGraphicsHud.fui","skinGraphicsInGame.fui",
        "skinGraphicsLabels.fui","skinLabels.fui","skinInGame.fui","skinHud.fui","skin.fui"})
        if(!LoadCheckMovie(folder,name,false))return 10;
    for(const char* name:{"HUD720.fui","InventoryMenu720.fui","InventoryMenuSplit720.fui"})
    {
        auto* file=LoadCheckMovie(folder,name,true);
        if(!file){std::printf("FAIL missing %s\n",name);return 11;}
        auto* root=file->getRootNode()->getFuiNode();
        if(auto* hud=dynamic_cast<FJ_Hud*>(root))
        {
            hud->SetNumBossBars(2);
            hud->SetBossBar(0,.75,L"Raid",BossEvent::PURPLE,BossEvent::PROGRESS);
            hud->SetBossBar(1,.5,L"Wither",BossEvent::PURPLE,BossEvent::PROGRESS);
            hud->UpdateStatusEffectTile(2,0,MobEffect::EMobEffectIcon_BadOmen,false,1);
            hud->UpdateStatusEffectTile(2,1,MobEffect::EMobEffectIcon_HeroOfTheVillage,true,1);
        }
        else if(auto* inventory=dynamic_cast<FJ_InventoryMenu*>(root))
        {
            inventory->AddEffect(MobEffect::EMobEffectIcon_BadOmen,L"Bad Omen",6000);
            inventory->AddEffect(MobEffect::EMobEffectIcon_HeroOfTheVillage,L"Hero of the Village",2400);
        }
        else {std::printf("FAIL factory class %s\n",name);return 12;}
        const float background[]={.15f,.2f,.25f,1};
        gRenderer->StartFrame();gRenderer->SetLayout(Renderer::ELayout_Interface);
        gRenderer->SetClearColour(background);gRenderer->Clear(3);
        gFui->render(file,0,0,1280,720);SaveUiImage(folder+"/"+name+".png");
        gFui->unloadScene(file);std::printf("PASS: rendered raid controls in %s\n",name);
    }
    auto* minecraft = Minecraft::GetInstance();
    auto* world = new MultiPlayerLevel(nullptr, new LevelSettings(1, GameType::SURVIVAL, false, false, false,
        LevelType::DEFAULT, 54, 3, 128, false, 0, 0, 0), 0, Difficulty::NORMAL, 63);
    User user(L"RaidUiCheck", L"");
    minecraft->mPlayer = boost::shared_ptr<MultiplayerLocalPlayer>(
        new MultiplayerLocalPlayer(minecraft, world, &user, nullptr));
    minecraft->mPlayer->mUserIndex = 0;
    auto* renderer = minecraft->mGameRenderer;
    const ScreenSizeCalculator screen(minecraft->mOptions, -1);
    for (const auto& icon : {L"village_hero_effect", L"bad_omen_effect"})
    {
        const ResourceLocation texture(std::wstring(L"misc/") + icon);
        renderer->displayIconActivation(&texture, 0);
        const float background[] = {.15f, .2f, .25f, 1};
        gRenderer->StartFrame();
        gRenderer->SetLayout(Renderer::ELayout_Interface);
        gRenderer->SetClearColour(background);
        gRenderer->Clear(3);
        renderer->setupGuiScreen(-1);
        renderer->renderItemActivationAnimation(screen.getWidth(), screen.getHeight(), .5f);
        GLint viewport[4];
        Gl::GetIntegerv(0x0BA2, viewport);
        std::vector<unsigned char> pixels(static_cast<size_t>(viewport[2]) * viewport[3] * 4);
        Gl::ReadPixels(0, 0, viewport[2], viewport[3], Gl::RGBA, Gl::UNSIGNED_BYTE, pixels.data());
        size_t coloredPixels = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            if (pixels[i] > 90 || pixels[i + 1] > 100 || pixels[i + 2] > 110) ++coloredPixels;
        if (coloredPixels < 100)
        {
            std::puts("FAIL: raid reward activation drew no icon");
            return 17;
        }
        const std::wstring filename(icon);
        SaveUiImage(folder + "/" + std::string(filename.begin(), filename.end()) + ".png");
        renderer->resetData();
    }
    minecraft->mPlayer.reset();
    std::puts("PASS: rendered the two native raid reward icons");
    const char* bankPath=std::getenv("LCE_RAID_SOUND_BANK");
    std::ifstream input(bankPath,std::ios::binary|std::ios::ate);
    std::vector<unsigned char> bytes(size_t(input.tellg()));input.seekg(0);input.read((char*)bytes.data(),bytes.size());
    PcSoundBank bank;std::string error;if(!bank.Load(bytes,&error)||!bank.Skipped().empty())return 13;
    int pcm=0,bink=0;
    for(const auto& sound:bank.Sounds())
    {
        PcAudioDecoder decoder;if(!decoder.Open(sound.data,sound.size))return 14;
        std::vector<float> frames(decoder.MaxBlockFrames()*decoder.Channels());
        unsigned count=0;int got=0;
        while((got=decoder.DecodeBlock(frames.data()))>0)count+=got;
        const bool isPcm=std::memcmp(sound.data,"RIFF",4)==0;
        if(got<0 || (isPcm && count!=decoder.TotalFrames())
            || (!isPcm && std::abs(static_cast<long long>(count)-decoder.TotalFrames())>decoder.MaxBlockFrames()))
        {std::printf("FAIL decode %s: decoded %u expected %u, final %d\n",sound.name.c_str(),count,decoder.TotalFrames(),got);return 15;}
        decoder.Rewind();if(decoder.DecodeBlock(frames.data())<=0)return 16;
        if(std::memcmp(sound.data,"RIFF",4)==0)++pcm;else ++bink;
    }
    std::printf("PASS: decoded/replayed %d PCM and %d Bink sounds\n",pcm,bink);
    std::puts("PASS: raid UI and audio regression complete");return 0;
}
