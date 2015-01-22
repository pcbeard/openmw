#ifndef OPENMW_ESSIMPORT_CONVERTER_H
#define OPENMW_ESSIMPORT_CONVERTER_H

#include <components/esm/esmreader.hpp>
#include <components/esm/esmwriter.hpp>

#include <components/esm/loadcell.hpp>
#include <components/esm/loadbook.hpp>
#include <components/esm/loadclas.hpp>
#include <components/esm/loadglob.hpp>
#include <components/esm/cellstate.hpp>
#include <components/esm/loadfact.hpp>
#include <components/esm/dialoguestate.hpp>
#include <components/esm/custommarkerstate.hpp>
#include <components/esm/loadcrea.hpp>

#include "importcrec.hpp"
#include "importcntc.hpp"

#include "importercontext.hpp"
#include "importcellref.hpp"
#include "importklst.hpp"
#include "importgame.hpp"
#include "importinfo.hpp"
#include "importdial.hpp"
#include "importques.hpp"
#include "importjour.hpp"
#include "importscpt.hpp"

#include "convertacdt.hpp"
#include "convertnpcc.hpp"

namespace ESSImport
{

class Converter
{
public:
    /// @return the order for writing this converter's records to the output file, in relation to other converters
    virtual int getStage() { return 1; }

    virtual ~Converter() {}

    void setContext(Context& context) { mContext = &context; }

    virtual void read(ESM::ESMReader& esm)
    {
    }

    /// Called after the input file has been read in completely, which may be necessary
    /// if the conversion process relies on information in other records
    virtual void write(ESM::ESMWriter& esm)
    {

    }

protected:
    Context* mContext;
};

/// Default converter: simply reads the record and writes it unmodified to the output
template <typename T>
class DefaultConverter : public Converter
{
public:
    virtual int getStage() { return 0; }

    virtual void read(ESM::ESMReader& esm)
    {
        std::string id = esm.getHNString("NAME");
        T record;
        record.load(esm);
        mRecords[id] = record;
    }

    virtual void write(ESM::ESMWriter& esm)
    {
        for (typename std::map<std::string, T>::const_iterator it = mRecords.begin(); it != mRecords.end(); ++it)
        {
            esm.startRecord(T::sRecordId);
            esm.writeHNString("NAME", it->first);
            it->second.save(esm);
            esm.endRecord(T::sRecordId);
        }
    }

protected:
    std::map<std::string, T> mRecords;
};

class ConvertNPC : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        ESM::NPC npc;
        std::string id = esm.getHNString("NAME");
        npc.load(esm);
        if (id != "player")
        {
            // TODO:
            // this should handle changes to the NPC struct, but since there is no index here
            // it will apply to ALL instances of the class. seems to be the reason for the
            // "feature" in MW where changing AI settings of one guard will change it for all guards of that refID.
        }
        else
        {
            mContext->mPlayer.mObject.mCreatureStats.mLevel = npc.mNpdt52.mLevel;
            mContext->mPlayerBase = npc;
            std::map<const int, float> empty;
            // FIXME: player start spells and birthsign spells aren't listed here,
            // need to fix openmw to account for this
            for (std::vector<std::string>::const_iterator it = npc.mSpells.mList.begin(); it != npc.mSpells.mList.end(); ++it)
                mContext->mPlayer.mObject.mCreatureStats.mSpells.mSpells[*it] = empty;

            // Clear the list now that we've written it, this prevents issues cropping up with
            // ensureCustomData() in OpenMW tripping over no longer existing spells, where an error would be fatal.
            mContext->mPlayerBase.mSpells.mList.clear();

            // Same with inventory. Actually it's strange this would contain something, since there's already an
            // inventory list in NPCC. There seems to be a fair amount of redundancy in this format.
            mContext->mPlayerBase.mInventory.mList.clear();
        }
    }
};

class ConvertCREA : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        // See comment in ConvertNPC
        ESM::Creature creature;
        std::string id = esm.getHNString("NAME");
        creature.load(esm);
    }
};

// Do we need ConvertCONT?
// I've seen a CONT record in a certain save file, but the container contents in it
// were identical to a corresponding CNTC record. See previous comment about redundancy...

class ConvertGlobal : public DefaultConverter<ESM::Global>
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        std::string id = esm.getHNString("NAME");
        ESM::Global global;
        global.load(esm);
        if (Misc::StringUtils::ciEqual(id, "gamehour"))
            mContext->mHour = global.mValue.getFloat();
        if (Misc::StringUtils::ciEqual(id, "day"))
            mContext->mDay = global.mValue.getInteger();
        if (Misc::StringUtils::ciEqual(id, "month"))
            mContext->mMonth = global.mValue.getInteger();
        if (Misc::StringUtils::ciEqual(id, "year"))
            mContext->mYear = global.mValue.getInteger();
        mRecords[id] = global;
    }
};

class ConvertClass : public DefaultConverter<ESM::Class>
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        std::string id = esm.getHNString("NAME");
        ESM::Class class_;
        class_.load(esm);

        if (id == "NEWCLASSID_CHARGEN")
            mContext->mCustomPlayerClassName = class_.mName;

        mRecords[id] = class_;
    }
};

class ConvertBook : public DefaultConverter<ESM::Book>
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        std::string id = esm.getHNString("NAME");
        ESM::Book book;
        book.load(esm);
        if (book.mData.mSkillID == -1)
            mContext->mPlayer.mObject.mNpcStats.mUsedIds.push_back(Misc::StringUtils::lowerCase(id));

        mRecords[id] = book;
    }
};

class ConvertNPCC : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        std::string id = esm.getHNString("NAME");
        NPCC npcc;
        npcc.load(esm);
        if (id == "PlayerSaveGame")
        {
            convertNPCC(npcc, mContext->mPlayer.mObject);
        }
        else
        {
            int index = npcc.mNPDT.mIndex;
            mContext->mNpcChanges.insert(std::make_pair(std::make_pair(index,id), npcc)).second;
        }
    }
};

class ConvertREFR : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        REFR refr;
        refr.load(esm);
        assert(refr.mRefID == "PlayerSaveGame");
        mContext->mPlayer.mObject.mPosition = refr.mPos;

        ESM::CreatureStats& cStats = mContext->mPlayer.mObject.mCreatureStats;
        convertACDT(refr.mActorData.mACDT, cStats);

        ESM::NpcStats& npcStats = mContext->mPlayer.mObject.mNpcStats;
        convertNpcData(refr.mActorData, npcStats);
    }
};

class ConvertPCDT : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        PCDT pcdt;
        pcdt.load(esm);

        mContext->mPlayer.mBirthsign = pcdt.mBirthsign;
        mContext->mPlayer.mObject.mNpcStats.mBounty = pcdt.mBounty;
        for (std::vector<PCDT::FNAM>::const_iterator it = pcdt.mFactions.begin(); it != pcdt.mFactions.end(); ++it)
        {
            ESM::NpcStats::Faction faction;
            faction.mExpelled = it->mFlags & 0x2;
            faction.mRank = it->mRank;
            faction.mReputation = it->mReputation;
            mContext->mPlayer.mObject.mNpcStats.mFactions[it->mFactionName.toString()] = faction;
        }
        for (int i=0; i<8; ++i)
            mContext->mPlayer.mObject.mNpcStats.mSkillIncrease[i] = pcdt.mPNAM.mSkillIncreases[i];
        mContext->mPlayer.mObject.mNpcStats.mLevelProgress = pcdt.mPNAM.mLevelProgress;

        for (std::vector<std::string>::const_iterator it = pcdt.mKnownDialogueTopics.begin();
             it != pcdt.mKnownDialogueTopics.end(); ++it)
        {
            mContext->mDialogueState.mKnownTopics.push_back(Misc::StringUtils::lowerCase(*it));
        }

    }
};

class ConvertCNTC : public Converter
{
    virtual void read(ESM::ESMReader &esm)
    {
        std::string id = esm.getHNString("NAME");
        CNTC cntc;
        cntc.load(esm);
        mContext->mContainerChanges.insert(std::make_pair(std::make_pair(cntc.mIndex,id), cntc));
    }
};

class ConvertCREC : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        std::string id = esm.getHNString("NAME");
        CREC crec;
        crec.load(esm);
        mContext->mCreatureChanges.insert(std::make_pair(std::make_pair(crec.mIndex,id), crec));
    }
};

class ConvertFMAP : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm);
};

class ConvertCell : public Converter
{
public:
    virtual void read(ESM::ESMReader& esm);
    virtual void write(ESM::ESMWriter& esm);

private:
    struct Cell
    {
        ESM::Cell mCell;
        std::vector<CellRef> mRefs;
        std::vector<unsigned int> mFogOfWar;
    };

    std::map<std::string, Cell> mIntCells;
    std::map<std::pair<int, int>, Cell> mExtCells;

    std::vector<ESM::CustomMarker> mMarkers;

    void writeCell(const Cell& cell, ESM::ESMWriter &esm);
};

class ConvertKLST : public Converter
{
public:
    virtual void read(ESM::ESMReader& esm)
    {
        KLST klst;
        klst.load(esm);
        mKillCounter = klst.mKillCounter;

        mContext->mPlayer.mObject.mNpcStats.mWerewolfKills = klst.mWerewolfKills;
    }

    virtual void write(ESM::ESMWriter &esm)
    {
        esm.startRecord(ESM::REC_DCOU);
        for (std::map<std::string, int>::const_iterator it = mKillCounter.begin(); it != mKillCounter.end(); ++it)
        {
            esm.writeHNString("ID__", it->first);
            esm.writeHNT ("COUN", it->second);
        }
        esm.endRecord(ESM::REC_DCOU);
    }

private:
    std::map<std::string, int> mKillCounter;
};

class ConvertFACT : public Converter
{
public:
    virtual void read(ESM::ESMReader& esm)
    {
        std::string id = esm.getHNString("NAME");
        ESM::Faction faction;
        faction.load(esm);

        Misc::StringUtils::toLower(id);
        for (std::map<std::string, int>::const_iterator it = faction.mReactions.begin(); it != faction.mReactions.end(); ++it)
        {
            std::string faction2 = Misc::StringUtils::lowerCase(it->first);
            mContext->mDialogueState.mChangedFactionReaction[id].insert(std::make_pair(faction2, it->second));
        }
    }
};

/// Stolen items
class ConvertSTLN : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        std::string itemid = esm.getHNString("NAME");

        while (esm.isNextSub("ONAM"))
        {
            std::string ownerid = esm.getHString();
            mStolenItems.insert(std::make_pair(itemid, ownerid));
        }
        while (esm.isNextSub("FNAM"))
        {
            std::string factionid = esm.getHString();
            mFactionStolenItems.insert(std::make_pair(itemid, factionid));
        }
    }
private:
    std::multimap<std::string, std::string> mStolenItems;
    std::multimap<std::string, std::string> mFactionStolenItems;
};

/// Seen responses for a dialogue topic?
/// Each DIAL record is followed by a number of INFO records, I believe, just like in ESMs
/// Dialogue conversion problems (probably have to adjust OpenMW format) -
/// - Journal is stored in one continuous HTML markup rather than each entry separately with associated info ID.
/// - Seen dialogue responses only store the INFO id, rather than the fulltext.
/// - Quest stages only store the INFO id, rather than the journal entry fulltext.
class ConvertINFO : public Converter
{
public:
    virtual void read(ESM::ESMReader& esm)
    {
        INFO info;
        info.load(esm);
    }
};

class ConvertDIAL : public Converter
{
public:
    virtual void read(ESM::ESMReader& esm)
    {
        std::string id = esm.getHNString("NAME");
        DIAL dial;
        dial.load(esm);
    }
};

class ConvertQUES : public Converter
{
public:
    virtual void read(ESM::ESMReader& esm)
    {
        std::string id = esm.getHNString("NAME");
        QUES quest;
        quest.load(esm);
    }
};

class ConvertJOUR : public Converter
{
public:
    virtual void read(ESM::ESMReader& esm)
    {
        JOUR journal;
        journal.load(esm);
    }
};

class ConvertGAME : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        GAME game;
        game.load(esm);
    }
};

/// Running global script
class ConvertSCPT : public Converter
{
public:
    virtual void read(ESM::ESMReader &esm)
    {
        SCPT script;
        script.load(esm);
    }
};

}

#endif
