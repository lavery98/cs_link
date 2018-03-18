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

/* Individual channel link entries */
struct LinkChannelEntry : Serializable
{
public:
  Anope::string chan;
  Anope::string linkchan;

  LinkChannelEntry() : Serializable("LinkChannel") { }

  LinkChannelEntry(ChannelInfo *c, const Anope::string &cLinkChan) : Serializable("LinkChannel")
  {
    this->chan = c->name;
    this->linkchan = cLinkChan;
  }

  ~LinkChannelEntry();

  void Serialize(Serialize::Data &data) anope_override
  {
    data["chan"] << this->chan;
    data["linkchan"] << this->linkchan;
  }

  static Serializable* Unserialize(Serializable *obj, Serialize::Data &data);
};

/* Per channel list of linked channels */
struct LinkChannelList : Serialize::Checker<std::vector<LinkChannelEntry *> >
{
public:
  LinkChannelList(Extensible *) : Serialize::Checker<std::vector<LinkChannelEntry *> >("LinkChannel") { }

  ~LinkChannelList()
  {
    for(unsigned i = (*this)->size(); i > 0; i--)
    {
      delete (*this)->at(i - 1);
    }
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

Serializable* LinkChannelEntry::Unserialize(Serializable *obj, Serialize::Data &data)
{
  Anope::string schan, slinkchan;

  data["chan"] >> schan;

  ChannelInfo *ci = ChannelInfo::Find(schan);
  if(!ci)
    return NULL;

  if(obj)
  {
    LinkChannelEntry *entry = anope_dynamic_static_cast<LinkChannelEntry *>(obj);
    entry->chan = ci->name;
    data["linkchan"] >> entry->linkchan;
    return entry;
  }

  data["linkchan"] >> slinkchan;
  /* TODO: check if linked chan exists */

  LinkChannelEntry *entry = new LinkChannelEntry(ci, slinkchan);

  LinkChannelList *entries = ci->Require<LinkChannelList>("linkchannellist");
  (*entries)->insert((*entries)->begin(), entry);
  return entry;
}

class CommandCSLink : public Command
{
  void DoAdd(CommandSource &source, ChannelInfo *ci, const std::vector<Anope::string> &params)
  {
  }

  void DoDel(CommandSource &source, ChannelInfo *ci, const std::vector<Anope::string> &params)
  {
  }

  void DoList(CommandSource &source, ChannelInfo *ci, const std::vector<Anope::string> &params)
  {
  }

public:
  CommandCSLink(Module *creator) : Command(creator, "chanserv/link", 2, 5)
  {
    this->SetDesc("Modify the list of linked channels");
    this->SetSyntax("\037channel\037 ADD \037channel\037 \037min-level\037 \037max-level\037");
    this->SetSyntax("\037channel\037 DEL \037channel\037");
    this->SetSyntax("\037channel\037 LIST");
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

    bool has_access = false;
    if(source.HasPriv("chanserv/link/modify"))
      has_access = true;
    else if(is_list && source.HasPriv("chanserv/link/list"))
      has_access = true;
    else if(is_list && source.AccessFor(ci).HasPriv("LINK_LIST"))
      has_access = true;
    else if(source.AccessFor(ci).HasPriv("LINK_CHANGE"))
      has_access = true;

    if(is_list ? 0 : (cmd.equals_ci("DEL") ? (channel.empty() || !min_level.empty() || !max_level.empty()) : max_level.empty()))
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
    else
      this->OnSyntaxError(source, "");

    return;
  }

  bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
  {
    this->SendSyntax(source);
    return true;
  }
};

class CSLink : public Module
{
  ExtensibleItem<LinkChannelList> linkchannellist;
  CommandCSLink commandcslink;

public:
  CSLink(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, THIRD),
        linkchannellist(this, "linkchannellist"), commandcslink(this)
  {
    this->SetAuthor("Ashley Lavery");
    this->SetVersion("1.0");
  }

  void OnAccessAdd(ChannelInfo *ci, CommandSource &source, ChanAccess *access) anope_override
  {
  }

  void OnAccessClear(ChannelInfo *ci, CommandSource &source) anope_override
  {
  }

  void OnAccessDel(ChannelInfo *ci, CommandSource &source, ChanAccess *access) anope_override
  {
  }
};

MODULE_INIT(CSLink)
