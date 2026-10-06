#pragma once
#include "haunted_lines.h"
#include <random>
#include <algorithm>
#include <array>
#include <limits>
namespace chungus_ui {
class HauntedPicker {
public:
    explicit HauntedPicker(uint64_t seed) : random(seed) {}
    const HauntedMessage& next() {
        // Preserve population rarity before applying per-family eligibility.
        std::uniform_int_distribution<size_t> population(0, hauntedMessages.size()-1);
        auto tier=hauntedMessages[population(random)].tier;
        if(previousRare&&hauntedRare(tier)) {
            do {tier=hauntedMessages[population(random)].tier;} while(hauntedRare(tier));
        }
        size_t chosen=choose(tier);
        // Six-entry tier can exhaust in an adversarial sequence. Stay bounded
        // and honor history rather than repeating or getting stuck rerolling.
        if(chosen==invalid)chosen=choose(255);
        const auto& message=hauntedMessages[chosen];
        recent[position]=chosen;position=(position+1)%recent.size();
        size=std::min(size+1,recent.size());previousRare=hauntedRare(message.tier);
        return message;
    }
private:
    std::mt19937_64 random;
    static constexpr size_t invalid=std::numeric_limits<size_t>::max();
    std::array<size_t,64> recent{};
    size_t position=0,size=0;
    bool previousRare=false;
    bool eligible(size_t index) const {
        const auto& candidate=hauntedMessages[index];
        if(previousRare&&hauntedRare(candidate.tier))return false;
        unsigned familyCount=0;
        for(size_t n=0;n<size;n++) {
            auto old=recent[(position+recent.size()-1-n)%recent.size()];
            if(old==index)return false;
            if(n<8&&hauntedMessages[old].family==candidate.family)familyCount++;
        }
        return familyCount<2;
    }
    size_t choose(uint8_t tier) {
        // Exact-uniform bounded rejection plus reservoir fallback. No I/O,
        // clock, identifiers, allocation, or external state of any kind.
        size_t begin=0,end=hauntedMessages.size();
        if(tier!=255) {
            constexpr size_t starts[]={0,4200,6000,6600,6660,6666};
            begin=starts[tier];end=starts[tier+1];
        }
        std::uniform_int_distribution<size_t> draw(begin,end-1);
        for(unsigned attempt=0;attempt<32;attempt++) {
            auto index=draw(random);if(eligible(index))return index;
        }
        size_t selected=invalid,count=0;
        for(size_t i=begin;i<end;i++)if(eligible(i)) {
            std::uniform_int_distribution<size_t> reservoir(0,count++);
            if(reservoir(random)==0)selected=i;
        }
        return selected;
    }
};
}
