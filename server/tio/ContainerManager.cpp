/*
Tio: The Information Overlord
Copyright 2010 Rodrigo Strauss (http://www.1bit.com.br)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "pch.h"
#include "ContainerManager.h"

namespace tio
{
	void ContainerManager::RegisterFundamentalStorageManagers(
		shared_ptr<ITioStorageManager> volatileVectorManager,
		shared_ptr<ITioStorageManager> volatileListManager,
		shared_ptr<ITioStorageManager> volatileMapManager)
	{
		tio_lock_guard lock(mutex_);
		
		volatileVectorManager->SetSubscriber(sink_);
		volatileListManager->SetSubscriber(sink_);
		volatileMapManager->SetSubscriber(sink_);


		managerByType_["volatile_vector"] = volatileVectorManager;
		managerByType_["volatile_list"] = volatileListManager;
		managerByType_["volatile_map"] = volatileMapManager;

		meta_containers_ = CreateContainer("volatile_map", "__meta__/containers");
		meta_availableTypes_ = CreateContainer("volatile_vector", "__meta__/available_types");

		//
		// it'll make it available on the __meta__/containers list itself...
		//
		meta_containers_ = CreateContainer("volatile_map", "__meta__/containers");
		
		meta_availableTypes_->PushBack(nullptr, "volatile_vector");
		meta_availableTypes_->PushBack(nullptr, "volatile_list");
		meta_availableTypes_->PushBack(nullptr, "volatile_map");
	}

	void ContainerManager::RegisterStorageManager(const string& type, shared_ptr<ITioStorageManager> manager)
	{
		tio_lock_guard lock(mutex_);

		manager->SetSubscriber(sink_);
		
		managerByType_[type] = manager;

		meta_availableTypes_->PushBack(TIONULL, type);

        std::vector<StorageInfo> storageList = manager->GetStorageList();

        BOOST_FOREACH(StorageInfo& si, storageList)
        {
            meta_containers_->Set(si.name, si.type);
        }
	}


	shared_ptr<ITioStorageManager> ContainerManager::GetStorageManagerByType(string type)
	{
		tio_lock_guard lock(mutex_);

		type = ResolveAlias(type);

		ManagerByType::iterator i = managerByType_.find(type);

		if(i == managerByType_.end())
			throw std::invalid_argument(string("invalid type: ") + type);

		return i->second;
	}

	shared_ptr<ITioContainer> ContainerManager::CreateOrOpen(string type, OperationType op, const string& name)
	{
		tio_lock_guard lock(mutex_);

		type = ResolveAlias(type);

		OpenContainersMap::const_iterator i = openContainers_.find(name);

		//
		// We will reuse container objects instead of having
		// more than one container object per storage
		//
		if(i != openContainers_.end() && !i->second.expired())
		{
			shared_ptr<ITioContainer> container = i->second.lock();
			
			// check if user didn't ask for wrong type
			//
			// Esse erro est?? acontecendo depois que eu
			// habilitei novamente o volatile_vector
			//
			// S?? acontece quando eu rodo dois tio_bench de
			// forma paralela, n??o consegui identificar o problema
			//
			//if(op == create && container->GetType() != type)
			//	throw std::runtime_error("invalid container type");

			return container;
		}

		shared_ptr<ITioStorage> storage;
		shared_ptr<ITioPropertyMap> propertyMap;

		if(op == create)
		{
			shared_ptr<ITioStorageManager> storageManager = GetStorageManagerByType(type);

			pair_assign(storage, propertyMap) = storageManager->CreateStorage(type, name);
			
			if(meta_containers_)
				meta_containers_->Set(name, type);
		}
		else if (op == open)
		{
			if(type.empty())
			{
				TioData value;
				meta_containers_->GetRecord(name, NULL, &value);
				type = value.AsSz();
			}

			shared_ptr<ITioStorageManager> storageManager = GetStorageManagerByType(type);

			pair_assign(storage, propertyMap) = storageManager->OpenStorage(type, name);
		}

		shared_ptr<ITioContainer> container(new Container(storage, propertyMap));

		openContainers_[name] = container;

		return container;
	}

	void ContainerManager::DeleteContainer(const string& type, const string& name)
	{
		tio_lock_guard lock(mutex_);

		string realType = ResolveAlias(type);
		shared_ptr<ITioStorageManager> storageManager = GetStorageManagerByType(realType);

		storageManager->DeleteStorage(realType, name);

		meta_containers_->Delete(name);
	}


	shared_ptr<ITioContainer> ContainerManager::CreateContainer(const string& type, const string& name)
	{
		return CreateOrOpen(type, create, name);
	}

	shared_ptr<ITioContainer> ContainerManager::OpenContainer(const string& type, const string& name)
	{
		return CreateOrOpen(type, open, name);
	}

	void ContainerManager::AddAlias(const string& alias, const string& type)
	{
		tio_lock_guard lock(mutex_);

		aliases_[alias] = type;

	}

	bool ContainerManager::Exists(const string& containerType, const string& containerName)
	{
		tio_lock_guard lock(mutex_);

		return GetStorageManagerByType(containerType)->Exists(containerType, containerName);
	}

	string ContainerManager::ResolveAlias(const string& type)
	{
		tio_lock_guard lock(mutex_);

		if(type.empty())
			return type;

		AliasesMap::const_iterator iAlias = aliases_.find(type);

		if(iAlias != aliases_.end())
			return iAlias->second;
		else
			return type;
	}
}
