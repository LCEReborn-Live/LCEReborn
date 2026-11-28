#include "game/item/CrossbowItem.hpp"

#include <algorithm>
#include <cmath>

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/audio/SoundSource.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/InteractionHand.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/Mth.hpp"
#include "game/core/StringIds.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/entity/mob/FireworksRocketEntity.hpp"
#include "game/entity/projectile/Arrow.hpp"
#include "game/entity/projectile/ArrowItem.hpp"
#include "game/item/EnchantmentHelper.hpp"
#include "game/item/Enchantments.hpp"
#include "game/item/Inventory.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/ui/ItemTooltipDataHolder.hpp"
#include "game/util/Random.hpp"
#include "game/util/Vec3.hpp"
#include "game/world/Level.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
const wchar_t* const CHARGED = L"Charged";
const wchar_t* const PROJECTILES = L"ChargedProjectiles";
const int USE_DURATION = 72000;

bool isArrow(not_null_ptr<ItemInstance> item)
{
    return !item->isEmpty() && dynamic_cast<ArrowItem*>(item->getItem()) != nullptr;
}

not_null_ptr<ItemInstance> findAmmo(const boost::shared_ptr<Player>& player)
{
    const InteractionHand::EInteractionHand hands[] = {
        InteractionHand::eInteractionHand_OffHand, InteractionHand::eInteractionHand_MainHand};
    for (InteractionHand::EInteractionHand hand : hands)
    {
        auto held = player->getItemInHand(hand);
        if (!held->isEmpty() && (held->getItem() == Items::fireworks || isArrow(held)))
            return held;
    }
    for (unsigned int slot = 0; slot < player->mInventory->getContainerSize(); ++slot)
    {
        auto stack = player->mInventory->getItem(slot);
        if (isArrow(stack))
            return stack;
    }
    return ItemInstance::EMPTY;
}

ListTag* getProjectiles(not_null_ptr<ItemInstance> item)
{
    auto* tag = item->getTag();
    return tag == nullptr ? nullptr : tag->fjSafeGetListTag(PROJECTILES, Tag::TAG_COMPOUND);
}

bool isRocket(CompoundTag* projectile)
{
    return projectile->getString(L"id") == L"minecraft:fireworks"
        || projectile->getString(L"id") == L"minecraft:firework_rocket";
}

not_null_ptr<ItemInstance> readProjectile(CompoundTag* projectile)
{
    // Legacy Console Edition uses the older registry name for firework rockets.
    if (projectile->getString(L"id") == L"minecraft:firework_rocket")
    {
        CompoundTag legacy;
        legacy.merge(projectile);
        legacy.putString(L"id", L"minecraft:fireworks");
        return not_null_ptr<ItemInstance>(new ItemInstance(&legacy));
    }
    return not_null_ptr<ItemInstance>(new ItemInstance(projectile));
}
}

CrossbowItem::CrossbowItem()
{
    setMaxStackSize(1);
    setMaxDamage(465);
    setAllowOffhand(true);
    // Display the existing crossbow recipe alongside the bow on the Tools tab.
    setBaseItemTypeAndMaterial(24, 17);
    setDescriptionId(IDS_LCE_ITEM_CROSSBOW, IDS_LCE_DESC_CROSSBOW);
    setIconName(L"crossbow");
}

int CrossbowItem::getChargeDuration(not_null_ptr<ItemInstance> item)
{
    const int level = EnchantmentHelper::getItemEnchantmentLevel(Enchantments::QUICK_CHARGE, item);
    return 25 - 5 * Mth::clamp(level, 0, 3);
}

bool CrossbowItem::isCharged(not_null_ptr<ItemInstance> item)
{
    auto* tag = item->getTag();
    auto* projectiles = getProjectiles(item);
    return tag && tag->getBoolean(CHARGED) && projectiles && !projectiles->isEmpty();
}

void CrossbowItem::loadProjectile(not_null_ptr<ItemInstance> crossbow, not_null_ptr<ItemInstance> ammo, int count)
{
    auto loaded = ammo->copy();
    loaded->setCount(1);
    auto* list = new ListTag(Tag::TAG_COMPOUND);
    for (int shot = 0; shot < count; ++shot)
        list->add(loaded->save(new CompoundTag()));
    crossbow->addTagElement(PROJECTILES, list);
    crossbow->getTag()->putBoolean(CHARGED, true);
}

void CrossbowItem::clearProjectiles(not_null_ptr<ItemInstance> crossbow)
{
    auto* tag = crossbow->getTag();
    if (!tag)
        return;
    tag->remove(PROJECTILES);
    tag->remove(CHARGED);
    if (tag->isEmpty())
        crossbow->setTag(nullptr);
}

InteractionResultHolder CrossbowItem::use(Level* level, boost::shared_ptr<Player> player,
                                          InteractionHand::EInteractionHand hand)
{
    auto item = player->getItemInHand(hand);
    if (isCharged(item))
    {
        if (!level->mIsClientSide)
            shoot(level, player, item);
        return InteractionResultHolder(InteractionResult::eInteractionResult_Success, item);
    }
    if (player->mAbilities.mInstabuild || !findAmmo(player)->isEmpty())
    {
        player->startUsingItem(hand);
        return InteractionResultHolder(InteractionResult::eInteractionResult_Success, item);
    }
    return InteractionResultHolder(InteractionResult::eInteractionResult_Fail, item);
}

int CrossbowItem::getUseDuration(not_null_ptr<ItemInstance>)
{
    return USE_DURATION;
}

int CrossbowItem::getUseAnimation(not_null_ptr<const ItemInstance>) const
{
    return UseAnim_crossbow;
}

int CrossbowItem::getEnchantmentValue()
{
    return 1;
}

void CrossbowItem::releaseUsing(not_null_ptr<ItemInstance> item, Level* level,
                               boost::shared_ptr<LivingEntity> entity, int)
{
    if (level->mIsClientSide || entity->getTicksUsingItem() < getChargeDuration(item) || isCharged(item))
        return;

    auto player = boost::dynamic_pointer_cast<Player>(entity);
    if (!player)
        return;
    auto ammo = findAmmo(player);
    if (ammo->isEmpty())
    {
        if (!player->mAbilities.mInstabuild)
            return;
        ammo = not_null_ptr<ItemInstance>(new ItemInstance(Items::arrow));
    }

    const int count = EnchantmentHelper::getItemEnchantmentLevel(Enchantments::MULTISHOT, item) > 0 ? 3 : 1;
    loadProjectile(item, ammo, count);

    if (!player->mAbilities.mInstabuild)
    {
        ammo->shrink(1);
        if (ammo->isEmpty())
            player->mInventory->removeItem(ammo);
    }
    const bool quickCharge = EnchantmentHelper::getItemEnchantmentLevel(Enchantments::QUICK_CHARGE, item) > 0;
    const auto* sound = quickCharge ? SoundEvents::CROSSBOW_QUICK_CHARGE_END : SoundEvents::CROSSBOW_LOADING_END;
    level->playSound({}, player->mX, player->mY, player->mZ, sound,
                     SoundSource::eSoundSource_Players, 1.0f, 1.0f, 16.0f);
}

void CrossbowItem::shoot(Level* level, const boost::shared_ptr<Player>& player, not_null_ptr<ItemInstance> item)
{
    auto* projectiles = getProjectiles(item);
    const int count = std::min(projectiles->size(), 3);
    const int piercing = EnchantmentHelper::getItemEnchantmentLevel(Enchantments::PIERCING, item);
    const float spread[] = {0.0f, -10.0f, 10.0f};
    const bool creative = player->mAbilities.mInstabuild;
    const Vec3* forward = player->getViewVector(1.0f);
    // Rotate about the shooter's up vector so the spread stays horizontal on screen.
    const double pitch = player->mXRot * Mth::DEG_TO_RAD;
    const double yaw = player->mYRot * Mth::DEG_TO_RAD;
    const double upX = -std::sin(yaw) * std::sin(pitch);
    const double upY = std::cos(pitch);
    const double upZ = std::cos(yaw) * std::sin(pitch);
    const double sideX = upY * forward->z - upZ * forward->y;
    const double sideY = upZ * forward->x - upX * forward->z;
    const double sideZ = upX * forward->y - upY * forward->x;
    int durability = 0;
    for (int shot = 0; shot < count; ++shot)
    {
        auto ammo = readProjectile(projectiles->getCompound(shot));
        if (ammo->isEmpty())
            continue;
        const double angle = spread[shot] * Mth::DEG_TO_RAD;
        const double x = forward->x * std::cos(angle) + sideX * std::sin(angle);
        const double y = forward->y * std::cos(angle) + sideY * std::sin(angle);
        const double z = forward->z * std::cos(angle) + sideZ * std::sin(angle);
        if (auto* arrowItem = dynamic_cast<ArrowItem*>(ammo->getItem()))
        {
            auto arrow = arrowItem->createArrow(level, ammo, player);
            arrow->setCrossbowShot(piercing);
            arrow->setCritArrow(true);
            if (creative || shot != 0)
                arrow->setPickup(AbstractRotatedItemProjectile::ePickup_CreativeOnly);
            arrow->shoot(x, y, z, 3.15f, 1.0f);
            level->addEntity(arrow);
            ++durability;
        }
        else if (ammo->getItem() == Items::fireworks)
        {
            auto rocket = boost::shared_ptr<FireworksRocketEntity>(new FireworksRocketEntity(
                level, player, ammo));
            rocket->shootFromCrossbow(x, y, z);
            level->addEntity(rocket);
            durability += 3;
        }
        else
        {
            continue;
        }
        const float soundPitch = shot == 0 ? 1.0f : (shot == 1 ? 0.8f : 1.2f);
        level->playSound({}, player->mX, player->mY, player->mZ, SoundEvents::CROSSBOW_SHOOT,
                         SoundSource::eSoundSource_Players, 1.0f, soundPitch, 16.0f);
    }
    clearProjectiles(item);
    if (durability > 0)
    {
        player->awardStat(GenericStats::itemsUsed(this), GenericStats::param_itemsUsed(player, item));
        item->hurtAndBreak(durability, player);
    }
}

void CrossbowItem::playLoadingSound(Level* level, const boost::shared_ptr<LivingEntity>& entity,
                                    not_null_ptr<ItemInstance> item, int ticks)
{
    const int duration = getChargeDuration(item);
    const bool start = ticks == (duration + 4) / 5;
    if (!start && ticks != (duration + 1) / 2)
        return;
    const bool quickCharge = EnchantmentHelper::getItemEnchantmentLevel(Enchantments::QUICK_CHARGE, item) > 0;
    const auto* sound = quickCharge
        ? (start ? SoundEvents::CROSSBOW_QUICK_CHARGE_START : SoundEvents::CROSSBOW_QUICK_CHARGE_MIDDLE)
        : (start ? SoundEvents::CROSSBOW_LOADING_START : SoundEvents::CROSSBOW_LOADING_MIDDLE);
    level->playSound({}, entity->mX, entity->mY, entity->mZ, sound,
                     SoundSource::eSoundSource_Players, 0.5f, 1.0f, 16.0f);
}

void CrossbowItem::appendHoverText(not_null_ptr<ItemInstance> item, boost::shared_ptr<Player> player,
                                   std::vector<HtmlString>* lines, bool advanced)
{
    if (!isCharged(item))
        return;
    auto ammo = readProjectile(getProjectiles(item)->getCompound(0));
    if (ammo->isEmpty())
        return;
    std::wstring label = L10N::GetString(IDS_LCE_CROSSBOW_PROJECTILE);
    lines->push_back(HtmlString(label + L" " + ammo->getHoverName(), eMinecraftColour_HTMLColor_7, false, false, 0));
    if (advanced && ammo->getItem() == Items::fireworks)
        ammo->getItem()->appendHoverText(ammo, player, lines, true);
}

unsigned int CrossbowItem::GetUseTooltip(const ItemTooltipDataHolder& data)
{
    if (isCharged(data.m_pItemInstance))
        return IDS_LCE_CROSSBOW_FIRE;
    if (data.m_pPlayer->isUsingItem())
        return IDS_LCE_CROSSBOW_LOAD;
    return data.m_pPlayer->mAbilities.mInstabuild || !findAmmo(data.m_pPlayer)->isEmpty()
        ? IDS_LCE_CROSSBOW_DRAW
        : IDS_NONE;
}

int CrossbowItem::GetOverrideCount()
{
    return 0;
}

void CrossbowItem::registerIcons(IconRegister* icons)
{
    Item::registerIcons(icons);
    mPullingIcons[0] = icons->registerIcon(L"crossbow_pulling_0");
    mPullingIcons[1] = icons->registerIcon(L"crossbow_pulling_1");
    mPullingIcons[2] = icons->registerIcon(L"crossbow_pulling_2");
    mLoadedArrowIcon = icons->registerIcon(L"crossbow_arrow");
    mLoadedRocketIcon = icons->registerIcon(L"crossbow_rocket");
}

TextureAtlasSprite* CrossbowItem::getIcon(not_null_ptr<ItemInstance> item)
{
    if (isCharged(item))
        return isRocket(getProjectiles(item)->getCompound(0)) ? mLoadedRocketIcon : mLoadedArrowIcon;
    return Item::getIcon(item);
}

TextureAtlasSprite* CrossbowItem::getLayerIcon(int aux, int layer, not_null_ptr<ItemInstance> item)
{
    return item ? getIcon(item) : Item::getLayerIcon(aux, layer, item);
}

TextureAtlasSprite* CrossbowItem::getPullingIcon(int ticks, not_null_ptr<ItemInstance> item)
{
    const int duration = getChargeDuration(item);
    const int stage = ticks * 10 >= duration * 9 ? 2 : (ticks * 10 >= duration * 6 ? 1 : 0);
    return mPullingIcons[stage];
}

TextureAtlasSprite* CrossbowItem::GetOverrideCountIcon(not_null_ptr<ItemInstance> item)
{
    auto player = Minecraft::GetInstance()->mPlayer;
    if (player && player->getUseItem() == item)
        return getPullingIcon(player->getTicksUsingItem(), item);
    return getIcon(item);
}
