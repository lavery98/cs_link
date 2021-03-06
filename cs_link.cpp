/**
 * Copyright 2018 Ashley Lavery <ashley@sa-irc.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "module.h"

struct LinkChannelEntry : Serializable
{
  Anope::string chan, linkchan;

  LinkChannelEntry() : Serializable("LinkChannel") { }

  ~LinkChannelEntry();

  void Serialize(Serialize::Data &data) const anope_override
  {
    data["chan"] << chan;
    data["linkchan"] << linkchan;
  }

  static Serializable *Unserialize(Serializable *obj, Serialize::Data &data);
};

struct LinkChannelList : Serialize::Checker<std::vector<LinkChannelEntry *> >
{
  LinkChannelList(Extensible *) : Serialize::Checker<std::vector<LinkChannelEntry *> >("LinkChannel") { }

  ~LinkChannelList()
  {
    for(unsigned i = (*this)->size(); i > 0; i--)
      delete (*this)->at(i - 1);
  }
};

LinkChannelEntry::~LinkChannelEntry()
{
  ChannelInfo *ci = ChannelInfo::Find(this->chan);
  if(!ci)
    return;

  LinkChannelList *entries = ci->GetExt<LinkChannelList>("linkchannellist");
  if(!entries)
    return;

  std::vector<LinkChannelEntry *>::iterator it = std::find((*entries)->begin(), (*entries)->end(), this);
  if(it != (*entries)->end())
    (*entries)->erase(it);
}

Serializable *LinkChannelEntry::Unserialize(Serializable *obj, Serialize::Data &data)
{
  Anope::string schan;
  data["chan"] >> schan;

  ChannelInfo *ci = ChannelInfo::Find(schan);
  if(!ci)
    return NULL;

  LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");
  LinkChannelEntry *entry;

  if(obj)
    entry = anope_dynamic_static_cast<LinkChannelEntry *>(obj);
  else
  {
    entry = new LinkChannelEntry();
    entry->chan = schan;
  }

  data["linkchan"] >> entry->linkchan;
  ChannelInfo *lci = ChannelInfo::Find(entry->linkchan);
  if(!lci)
    return NULL;

  if(!obj)
    (*entries)->push_back(entry);

  return entry;
}

class CommandCSLink : public Command
{
  void DoAdd(CommandSource &source, ChannelInfo *ci, const std::vector<Anope::string> &params)
  {
    Anope::string channel = params[2];

    ChannelInfo *lci = ChannelInfo::Find(channel);
    if(lci == NULL)
    {
      source.Reply(CHAN_X_NOT_REGISTERED, channel.c_str());
      return;
    }

    bool override = false;

    // check permissions for other channel
    if(!source.HasPriv("chanserv/administration") || !source.AccessFor(lci).HasPriv("FOUNDER"))
    {
      source.Reply(ACCESS_DENIED);
      return;
    }

    if((!source.AccessFor(ci).HasPriv("FOUNDER") || !source.AccessFor(lci).HasPriv("FOUNDER")) && source.HasPriv("chanserv/administration"))
      override = true;

    LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");

    // check if the channel is already linked
    for(unsigned i = 0; i < (*entries)->size(); i++)
    {
      LinkChannelEntry *entry = (*entries)->at(i);

      if(lci->name.equals_ci(entry->linkchan))
      {
        source.Reply("Channel %s is already linked to %s.", lci->name.c_str(), ci->name.c_str());
        return;
      }
    }

    LinkChannelEntry *entry = new LinkChannelEntry();
    entry->chan = ci->name;
    entry->linkchan = lci->name;
    (*entries)->insert((*entries)->begin(), entry);

    LinkChannelList *lentries = lci->Require<LinkChannelList>("linkchannellist");

    LinkChannelEntry *lentry = new LinkChannelEntry();
    lentry->chan = lci->name;
    lentry->linkchan = ci->name;
    (*lentries)->insert((*lentries)->begin(), lentry);

    //TODO: sync access

    Log(override ? LOG_OVERRIDE : LOG_COMMAND, source, this, ci) << "to add " << lci->name.c_str();
    source.Reply("\002%s\002 added to %s linked channel list.", lci->name.c_str(), ci->name.c_str());
  }

  void DoDel(CommandSource &source, ChannelInfo *ci, const std::vector<Anope::string> &params)
  {
    Anope::string channel = params[2];

    LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");
    if((*entries)->empty())
      source.Reply("%s linked channel list is empty.", ci->name.c_str());
    else if(isdigit(channel[0]) && channel.find_first_not_of("1234567890,-") == Anope::string::npos)
    {
      class LinkDelCallback : public NumberList
      {
        CommandSource &source;
        ChannelInfo *ci;
        Command *c;
        LinkChannelList *entries;
        unsigned deleted;
        Anope::string Channels;
        bool override;

      public:
        LinkDelCallback(CommandSource &_source, ChannelInfo *_ci, Command *_c, LinkChannelList *_entries, const Anope::string &numlist) : NumberList(numlist, true), source(_source), ci(_ci), c(_c), entries(_entries), deleted(0), override(false)
        {
          if(!source.AccessFor(ci).HasPriv("FOUNDER") && source.HasPriv("chanserv/administration"))
            this->override = true;
        }

        ~LinkDelCallback()
        {
          if(!deleted)
            source.Reply("No matching entries on %s linked channel list.", ci->name.c_str());
          else
          {
            Log(override ? LOG_OVERRIDE : LOG_COMMAND, source, c, ci) << "to delete " << Channels;
            if(deleted == 1)
              source.Reply("Deleted 1 entry from %s linked channel list.", ci->name.c_str());
            else
              source.Reply("Deleted %d entries from %s linked channel list.", deleted, ci->name.c_str());
          }
        }

        void HandleNumber(unsigned Number) anope_override
        {
          if(!Number || Number > (*entries)->size())
            return;

          LinkChannelEntry *entry = (*entries)->at(Number - 1);

          deleted++;
          if(!Channels.empty())
            Channels += ", " + entry->linkchan;
          else
            Channels = entry->linkchan;

          // TODO: might crash?
          delete (*entries)->at(Number - 1);
        }
      }
      delcallback(source, ci, this, entries, channel);
      delcallback.Process();
    }
    else
    {
      for(unsigned i = (*entries)->size(); i > 0; i--)
      {
        if(channel.equals_ci((*entries)->at(i - 1)->linkchan))
        {
          source.Reply("\002%s\002 deleted from %s linked channel list.", channel.c_str(), ci->name.c_str());
          bool override = !source.AccessFor(ci).HasPriv("FOUNDER") && source.HasPriv("chanserv/administration");
          Log(override ? LOG_OVERRIDE : LOG_COMMAND, source, this, ci) << "to delete " << channel.c_str();

          ChannelInfo *lci = ChannelInfo::Find(channel);
          if(lci != NULL)
          {
            LinkChannelList *lentries = lci->Require<LinkChannelList>("linkchannellist");
            if(!(*lentries)->empty())
            {
              for(unsigned j = (*lentries)->size(); j > 0; j--)
              {
                if(ci->name.equals_ci((*lentries)->at(j - 1)->linkchan))
                {
                  delete (*lentries)->at(j - 1);
                  break;
                }
              }
            }
          }

          delete (*entries)->at(i - 1);
          return;
        }
      }

      source.Reply("\002%s\002 not found on %s linked channel list.", channel.c_str(), ci->name.c_str());
    }

    return;
  }

  void DoList(CommandSource &source, ChannelInfo *ci, const std::vector<Anope::string> &params)
  {
    const Anope::string &channel = params.size() > 2 ? params[2] : "";

    LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");

    if((*entries)->empty())
    {
      source.Reply("%s linked channel list is empty.", ci->name.c_str());
      return;
    }

    ListFormatter list(source.GetAccount());
    list.AddColumn("Number").AddColumn("Channel");

    if(!channel.empty() && channel.find_first_not_of("1234567890,-") == Anope::string::npos)
    {
      class LinkListCallback : public NumberList
      {
        ListFormatter &list;
        ChannelInfo *ci;
        LinkChannelList *entries;

      public:
        LinkListCallback(ListFormatter &_list, ChannelInfo *_ci, LinkChannelList *_entries, const Anope::string &numlist) : NumberList(numlist, false), list(_list), ci(_ci), entries(_entries)
        {
        }

        void HandleNumber(unsigned number) anope_override
        {
          if(!number || number > (*entries)->size())
            return;

          LinkChannelEntry *entry = (*entries)->at(number - 1);

          ListFormatter::ListEntry le;
          le["Number"] = stringify(number);
          le["Channel"] = entry->linkchan;
          this->list.AddEntry(le);
        }
      }
      nl_list(list, ci, entries, channel);
      nl_list.Process();
    }
    else
    {
      for(unsigned i = 0; i < (*entries)->size(); i++)
      {
        LinkChannelEntry *entry = (*entries)->at(i);

        ListFormatter::ListEntry le;
        le["Number"] = stringify(i + 1);
        le["Channel"] = entry->linkchan;
        list.AddEntry(le);
      }
    }

    if(list.IsEmpty())
      source.Reply("No matching entries on %s linked channel list.", ci->name.c_str());
    else
    {
      std::vector<Anope::string> replies;
      list.Process(replies);

      source.Reply("Linked channel list for %s:", ci->name.c_str());

      for(unsigned i = 0; i < replies.size(); i++)
        source.Reply(replies[i]);

      source.Reply("End of linked channel list");
    }
  }

  void DoClear(CommandSource &source, ChannelInfo *ci, const std::vector<Anope::string> &params)
  {
    if(!source.IsFounder(ci) && !source.HasPriv("chanserv/administration"))
      source.Reply(ACCESS_DENIED);
    else
    {
      ci->Shrink<LinkChannelList>("linkchannellist");

      source.Reply("Channel %s linked channel list has been cleared.", ci->name.c_str());
    }
  }

public:
  CommandCSLink(Module *creator) : Command(creator, "chanserv/link", 2, 5)
  {
    this->SetDesc("Modify the list of linked channels");
    this->SetSyntax("\037channel\037 ADD \037channel\037 \037min-level\037 \037max-level\037");
    this->SetSyntax("\037channel\037 DEL \037channel\037");
    this->SetSyntax("\037channel\037 LIST");
    this->SetSyntax("\037channel\037 CLEAR");
  }

  void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
  {
    const Anope::string &cmd = params[1];
    const Anope::string &channel = params.size() > 2 ? params[2] : "";
    const Anope::string &min_level = params.size() > 3 ? params[3] : "";
    const Anope::string &max_level = params.size() > 4 ? params[4] : "";

    ChannelInfo *ci = ChannelInfo::Find(params[0]);
    if(ci == NULL)
    {
      source.Reply(CHAN_X_NOT_REGISTERED, params[0].c_str());
      return;
    }

    bool is_list = cmd.equals_ci("LIST");
    bool is_clear = cmd.equals_ci("CLEAR");

    bool has_access = false;
    if(source.HasPriv("chanserv/link/modify"))
      has_access = true;
    else if(is_list && source.HasPriv("chanserv/administration"))
      has_access = true;
    else if(is_list && source.AccessFor(ci).HasPriv("ACCESS_LIST"))
      has_access = true;
    else if(source.AccessFor(ci).HasPriv("FOUNDER"))
      has_access = true;

    if(is_list || is_clear ? 0 : (cmd.equals_ci("DEL") ? (channel.empty() || !min_level.empty() || !max_level.empty()) : max_level.empty()))
      this->OnSyntaxError(source, cmd);
    else if(!has_access)
      source.Reply(ACCESS_DENIED);
    else if(Anope::ReadOnly && !is_list)
      source.Reply("Sorry, channel link list modification is temporarily disabled.");
    else if(cmd.equals_ci("ADD"))
      this->DoAdd(source, ci, params);
    else if(cmd.equals_ci("DEL"))
      this->DoDel(source, ci, params);
    else if(cmd.equals_ci("LIST"))
      this->DoList(source, ci, params);
    else if(cmd.equals_ci("CLEAR"))
      this->DoClear(source, ci, params);
    else
      this->OnSyntaxError(source, "");

    return;
  }

  bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
  {
    this->SendSyntax(source);
    source.Reply(" ");
    source.Reply("Maintains the \002linked channel list\002 for a channel.  The linked\n"
                 "channel list specifies which channels the access list is linked to.  \n"
                 "When an access level is changed on a linked channel the access\n"
                 "level is also changed on this channel if the access level is within\n"
                 "the minimum and maximum levels specified.");
    source.Reply(" ");
    source.Reply("The \002LINK ADD\002 command adds the given channel to the linked\n"
                 "channel list with the given minimum and maximum access levels.\n");
    source.Reply(" ");
    source.Reply("The \002LINK DEL\002 command removes the given channel from the\n"
                 "linked channel list.  If a list of entry numbers is given, those\n"
                 "entries are deleted.  (See the example for LIST below.)");
    source.Reply(" ");
    source.Reply("The \002LINK LIST\002 command displays the linked channel list. If a\n"
                 "list of entry numbers is given, only those entries are shown; for\n"
                 "example:\n"
                 "  \002LINK #channel LIST 2-5,7-9\002\n"
                 "      Lists linked channel entries numbered 2 through 5 and\n"
                 "      7 though 9.\n"
                 "The \002LINK CLEAR\002 command clears all entries on the linked\n"
                 "channel list.");
    return true;
  }
};

class CSLink : public Module
{
  ExtensibleItem<LinkChannelList> linkchannellist;
  Serialize::Type linkchannel_type;
  CommandCSLink commandcslink;

public:
  CSLink(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
        linkchannellist(this, "linkchannellist"),
        linkchannel_type("LinkChannel", LinkChannelEntry::Unserialize),
        commandcslink(this)
  {
    this->SetAuthor("Ashley Lavery");
    this->SetVersion("1.0");
  }

  void OnAccessAdd(ChannelInfo *ci, CommandSource &source, ChanAccess *access) anope_override
  {
    LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");

    if(!(*entries)->empty())
    {
      for(unsigned i = (*entries)->size(); i > 0; i--)
      {
        LinkChannelEntry *entry = (*entries)->at(i - 1);

        ChannelInfo *lci = ChannelInfo::Find(entry->linkchan);
        if(!lci)
        {
          // Delete stale link
          delete (*entries)->at(i - 1);
          continue;
        }

        // Delete old access
        for(unsigned j = lci->GetAccessCount(); j > 0; j--)
        {
          const ChanAccess *laccess = lci->GetAccess(j - 1);
          // Don't keep adding an access that already exists
          if(laccess == access)
            return;

          if(laccess->GetAccount() == access->GetAccount() || laccess->Mask().equals_ci(access->Mask()))
          {
            delete lci->EraseAccess(j - 1);
            break;
          }
        }

        // Add access to channel
        lci->AddAccess(access);

        FOREACH_MOD(OnAccessAdd, (lci, source, access));

        Log(LOG_DEBUG) << source.GetNick() << " used linked access on " << lci->name << " to add " << access->Mask() << " with level " << access->AccessSerialize();
      }
    }
  }

  void OnAccessClear(ChannelInfo *ci, CommandSource &source) anope_override
  {
    LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");

    if(!(*entries)->empty())
    {
      for(unsigned i = (*entries)->size(); i > 0; i--)
      {
        LinkChannelEntry *entry = (*entries)->at(i - 1);

        ChannelInfo *lci = ChannelInfo::Find(entry->linkchan);
        if(!lci)
        {
          // Delete stale link
          delete (*entries)->at(i - 1);
          continue;
        }

        // Don't clear access if it is already empty
        if(!lci->GetAccessCount())
          return;

        lci->ClearAccess();

        FOREACH_MOD(OnAccessClear, (lci, source));

        Log(LOG_DEBUG) << source.GetNick() << " used linked access on " << lci->name << "to clear the access list";
      }
    }
  }

  void OnAccessDel(ChannelInfo *ci, CommandSource &source, ChanAccess *access) anope_override
  {
    LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");

    if(!(*entries)->empty())
    {
      for(unsigned i = (*entries)->size(); i > 0; i--)
      {
        LinkChannelEntry *entry = (*entries)->at(i - 1);

        ChannelInfo *lci = ChannelInfo::Find(entry->linkchan);
        if(!lci)
        {
          // Delete stale link
          delete (*entries)->at(i - 1);
          continue;
        }

        for(unsigned j = lci->GetAccessCount(); j > 0; j--)
        {
          ChanAccess *laccess = lci->GetAccess(j - 1);
          if(laccess->Mask().equals_ci(access->Mask()))
          {
            Log(LOG_DEBUG) << source.GetNick() << " used linked access on " << lci->name << " to delete " << access->Mask();

            lci->EraseAccess(j - 1);
            FOREACH_MOD(OnAccessDel, (lci, source, laccess));
            delete laccess;
            return;
          }
        }
      }
    }
  }

  void OnDelChan(ChannelInfo *ci) anope_override
  {
    LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");

    if(!(*entries)->empty())
    {
      for(unsigned i = 0; i < (*entries)->size(); i++)
      {
        LinkChannelEntry *entry = (*entries)->at(i);

        ChannelInfo *lci = ChannelInfo::Find(entry->linkchan);
        if(!lci)
          continue;

        LinkChannelList *lentries = lci->Require<LinkChannelList>("linkchannellist");

        if(!(*lentries)->empty())
        {
          for(unsigned j = (*lentries)->size(); j > 0; j--)
          {
            LinkChannelEntry *lentry = (*lentries)->at(j - 1);

            if(lentry->linkchan.equals_ci(entry->chan))
            {
              Log(LOG_DEBUG) << ci->name.c_str() << " is being deleted so removing link to " << lentry->chan.c_str();

              delete (*lentries)->at(j - 1);
              break;
            }
          }
        }
      }
    }
  }
};

MODULE_INIT(CSLink)
