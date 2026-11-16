#ifndef GAME_CORE_SHIELDSTRINGS_HPP
#define GAME_CORE_SHIELDSTRINGS_HPP

#include "game/core/XLanguage.hpp"

// Supplement the original language archive without modifying the user's assets.
// Select on each lookup so changing the game language also updates existing shields.
inline const wchar_t* LocalizedShieldString(unsigned int language, bool description)
{
    switch (language)
    {
    case XC_LANGUAGE_JAPANESE:
        return description ? L"スニークすると盾を構えて攻撃を防ぎます。" : L"盾";
    case XC_LANGUAGE_GERMAN:
        return description ? L"Schleiche, um den Schild zu heben und Angriffe abzuwehren." : L"Schild";
    case XC_LANGUAGE_FRENCH:
        return description ? L"Accroupissez-vous pour lever le bouclier et bloquer les attaques." : L"Bouclier";
    case XC_LANGUAGE_SPANISH:
        return description ? L"Agáchate para levantar el escudo y bloquear ataques." : L"Escudo";
    case XC_LANGUAGE_ITALIAN:
        return description ? L"Accovacciati per alzare lo scudo e bloccare gli attacchi." : L"Scudo";
    case XC_LANGUAGE_KOREAN:
        return description ? L"웅크리면 방패를 들어 공격을 막습니다." : L"방패";
    case XC_LANGUAGE_TCHINESE:
        return description ? L"潛行時可舉起盾牌並格擋攻擊。" : L"盾牌";
    case XC_LANGUAGE_PORTUGUESE:
        return description ? L"Agacha-te para levantar o escudo e bloquear ataques." : L"Escudo";
    case XC_LANGUAGE_SCHINESE:
    case XC_LANGUAGE_ZH_HANS:
        return description ? L"潜行时可举起盾牌并格挡攻击。" : L"盾牌";
    case XC_LANGUAGE_POLISH:
        return description ? L"Skradaj się, aby podnieść tarczę i blokować ataki." : L"Tarcza";
    case XC_LANGUAGE_RUSSIAN:
        return description ? L"Присядьте, чтобы поднять щит и блокировать атаки." : L"Щит";
    case XC_LANGUAGE_SWEDISH:
        return description ? L"Smyg för att höja skölden och blockera attacker." : L"Sköld";
    case XC_LANGUAGE_TURKISH:
        return description ? L"Kalkanı kaldırmak ve saldırıları engellemek için eğil." : L"Kalkan";
    case XC_LANGUAGE_NORWEGIAN:
        return description ? L"Snik for å løfte skjoldet og blokkere angrep." : L"Skjold";
    case XC_LANGUAGE_DUTCH:
        return description ? L"Sluip om het schild op te heffen en aanvallen te blokkeren." : L"Schild";
    case XC_LANGUAGE_FINNISH:
        return description ? L"Hiivi nostaaksesi kilven ja torjuaksesi hyökkäyksiä." : L"Kilpi";
    case XC_LANGUAGE_GREEK:
        return description ? L"Σκύψε για να σηκώσεις την ασπίδα και να αποκρούσεις επιθέσεις." : L"Ασπίδα";
    case XC_LANGUAGE_DANISH:
        return description ? L"Snig dig for at hæve skjoldet og blokere angreb." : L"Skjold";
    case XC_LANGUAGE_CZECH:
        return description ? L"Přikrč se, abys zvedl štít a zablokoval útoky." : L"Štít";
    case XC_LANGUAGE_SLOVAK:
        return description ? L"Prikrč sa, aby si zdvihol štít a zablokoval útoky." : L"Štít";
    default:
        return description ? L"Sneak to raise the shield and block attacks." : L"Shield";
    }
}

#endif
