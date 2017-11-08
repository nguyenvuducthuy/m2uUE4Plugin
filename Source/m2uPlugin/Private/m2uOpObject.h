#pragma once
// Operations on Objects (Actors)

#include "ActorEditorUtils.h"
#include "UnrealEd.h"

#include "m2uHelper.h"
#include "m2uOperation.h"


class Fm2uOpObjectTransform : public Fm2uOperation
{
public:

	Fm2uOpObjectTransform(Fm2uOperationManager* Manager=nullptr)
		:Fm2uOperation(Manager){}

	bool Execute(FString Cmd, FString& Result) override
	{
		const TCHAR* Str = *Cmd;
		bool DidExecute = false;

		if (FParse::Command(&Str, TEXT("TransformObject")))
		{
			Result = this->TransformObject(Str);
			DidExecute = true;
		}

		return DidExecute;
	}

	FString TransformObject(const TCHAR* Str)
	{
		TSharedPtr<FJsonObject> Object;
		auto Reader = TJsonReaderFactory<TCHAR>::Create(Str);
		if (!FJsonSerializer::Deserialize(Reader, Object)) {
			UE_LOG(LogM2U, Error, TEXT("Error translating Json data."));
			return TEXT("Failed");
		}
		if (!Object.IsValid()) {
			UE_LOG(LogM2U, Error, TEXT("Translated Json object not valid."));
			return TEXT("Failed");
		}

		const TArray<TSharedPtr<FJsonValue>>* FloatList = nullptr;
		if (!Object->TryGetArrayField(TEXT("matrix"), FloatList)) {
			return TEXT("Failed");
		}
		FMatrix Matrix;
		Matrix.M[0][0] = (*FloatList)[ 0]->AsNumber();
		Matrix.M[0][1] = (*FloatList)[ 1]->AsNumber();
		Matrix.M[0][2] = (*FloatList)[ 2]->AsNumber();
		Matrix.M[0][3] = (*FloatList)[ 3]->AsNumber();
		Matrix.M[1][0] = (*FloatList)[ 4]->AsNumber();
		Matrix.M[1][1] = (*FloatList)[ 5]->AsNumber();
		Matrix.M[1][2] = (*FloatList)[ 6]->AsNumber();
		Matrix.M[1][3] = (*FloatList)[ 7]->AsNumber();
		Matrix.M[2][0] = (*FloatList)[ 8]->AsNumber();
		Matrix.M[2][1] = (*FloatList)[ 9]->AsNumber();
		Matrix.M[2][2] = (*FloatList)[10]->AsNumber();
		Matrix.M[2][3] = (*FloatList)[11]->AsNumber();
		Matrix.M[3][0] = (*FloatList)[12]->AsNumber();
		Matrix.M[3][1] = (*FloatList)[13]->AsNumber();
		Matrix.M[3][2] = (*FloatList)[14]->AsNumber();
		Matrix.M[3][3] = (*FloatList)[15]->AsNumber();

		AActor* Actor = nullptr;
		const FString ActorName = Object->GetStringField(TEXT("name"));
		// UE_LOG(LogM2U, Error, TEXT("Would transform object %s with %s"), *ActorName, *Matrix.ToString());
		if (!m2uHelper::GetActorByName(*ActorName, &Actor) ||
		    Actor == nullptr)
		{
			UE_LOG(LogM2U, Log, TEXT("Actor %s not found or invalid."),
			       *ActorName);
			return TEXT("NotFound");
		}
		FTransform Transform(Matrix);
		FVector Translation = Transform.GetTranslation();
		Translation.Y = -Translation.Y;
		FRotator Rotator = Transform.GetRotation().Rotator();
		FRotator Temp = Rotator;
		Rotator.Roll = -Temp.Roll;
		Rotator.Pitch = Temp.Pitch;
		Rotator.Yaw = -Temp.Yaw;

		Transform.SetTranslation(Translation);
		Transform.SetRotation(FQuat(Rotator));

		Actor->SetActorTransform(Transform);
		return TEXT("Ok");
	}
};


class Fm2uOpObjectName : public Fm2uOperation
{
public:

	Fm2uOpObjectName(Fm2uOperationManager* Manager=nullptr)
		:Fm2uOperation(Manager){}

	bool Execute(FString Cmd, FString& Result) override
	{
		const TCHAR* Str = *Cmd;
		bool DidExecute = false;

		if (FParse::Command(&Str, TEXT("GetFreeName")))
		{
			const FString InName = FParse::Token(Str,0);
			FName FreeName = m2uHelper::GetFreeName(InName);
			Result = FreeName.ToString();

			DidExecute = true;
		}

		else if (FParse::Command(&Str, TEXT("RenameObject")))
		{
			const FString ActorName = FParse::Token(Str,0);
			// jump over the next space
			Str = FCString::Strchr(Str,' ');
			if( Str != NULL)
				Str++;
			// the desired new name
			const FString NewName = FParse::Token(Str,0);

			// find the Actor
			AActor* Actor = NULL;
			if(!m2uHelper::GetActorByName(*ActorName, &Actor) || Actor == NULL)
			{
				UE_LOG(LogM2U, Log, TEXT("Actor %s not found or invalid."), *ActorName);
				Result = TEXT("NotFound");
			}

			// try to rename the actor
			const FName ResultName = RenameActor(Actor, NewName);
			Result = ResultName.ToString();

			DidExecute = true;
		}

		return DidExecute;
	}

	/**
	 * FName RenameActor( AActor* Actor, const FString& Name)
	 *
	 * Try to set the Actor's FName to the desired name, while also
	 * setting the Label to the exact same name as the FName has
	 * resulted in.  The returned FName may differ from the desired
	 * name, if that was not valid or already in use.
	 *
	 * @param Actor The Actor which to edit
	 * @param Name The desired new name as a string
	 *
	 * @return FName The resulting ID (and Label-string)
	 *
	 * The Label is a friendly name that is displayed everywhere in
	 * the Editor and it can take special characters the FName can
	 * not. The FName is referred to as the object ID in the
	 * Editor. Labels need not be unique, the ID must.
	 *
	 * There are a few functions which are used to set Actor Labels
	 * and Names, but all allow a desync between Label and FName, and
	 * sometimes don't change the FName at all if deemed not
	 * necessary.
	 *
	 * But we want to be sure that the name we provide as FName is
	 * actually set *if* the name is still available.  And we want to
	 * be sure the Label is set to represent the FName exactly. It
	 * might be very confusing to the user if the Actor's "Name" as
	 * seen in the Outliner in the Editor is different from the name
	 * seen in the Program, but the objects still are considered to
	 * have the same name, because the FName is correct, but the Label
	 * is desynced.
	 *
	 * What we have is:
	 * 'SetActorLabel', which is the recommended way of changing the
	 * name. This function will set the Label immediately and then try
	 * to set the ID using the Actor's 'Rename' method, with names
	 * generated by 'MakeObjectNameFromActorLabel' or
	 * 'MakeUniqueObjectName'. The Actor's Label and ID are not
	 * guaranteed to match when using 'SetActorLabel'.
	 * 'MakeObjectNameFromActorLabel' will return a name, stripped of
	 * all invalid characters. But if the names are the same, and the
	 * ID has a number suffix and the label not, the returned name
	 * will not be changed.  (rename "Chair_5" to "Chair" will return
	 * "Chair_5" although I wanted "Chair") So even using
	 * 'SetActorLabel' to set the FName to something unique, based on
	 * the Label, and then setting the Label to what ID was returned,
	 * is not an option, because the ID might not result in what we
	 * provided, even though the name is free and valid.
	 */
	FName RenameActor(AActor* Actor, const FString& Name)
	{
		FString GeneratedName = m2uHelper::MakeValidNameString(Name);
		// Is there still a name, or was it stripped completely?
		// We don't change the name then. The calling function should
		// check this and inform the user.
		if (GeneratedName.IsEmpty())
		{
			return Actor->GetFName();
		}

		FName NewFName(*GeneratedName);
		// Check if name is "None", NAME_None, that is a valid name to
		// assign but in maya the name will be something like "_110"
		// while here it will be "None" with no number. So although
		// renaming "succeeded", the names differ.
		// If that is the case, we use a 'default' base name.
		if (NewFName == NAME_None)
		{
			NewFName = FName(*m2uHelper::M2U_GENERATED_NAME);
		}

		if (Actor->GetFName() == NewFName)
		{
			// The new name and current name are the same. Either the
			// input was the same, or they differed by invalid chars.
			return Actor->GetFName();
		}

		UObject* NewOuter = nullptr; // NULL = Use the current Outer
		ERenameFlags RenFlags = REN_DontCreateRedirectors;
		int32 Flags = REN_Test | REN_DoNotDirty | REN_NonTransactional | RenFlags;
		bool CanRename = Actor->Rename(*NewFName.ToString(), NewOuter, Flags);
		if (CanRename)
		{
			Actor->Rename(*NewFName.ToString(), NewOuter, RenFlags);
		}
		else
		{
			// Unable to rename the Actor to that name.
			return Actor->GetFName();
		}

		const FName ResultFName = Actor->GetFName();
		// Set the actor label to represent the ID
		Actor->SetActorLabel(ResultFName.ToString());
		return ResultFName;
	} // FName RenameActor()
};


class Fm2uOpObjectDelete : public Fm2uOperation
{
public:

	Fm2uOpObjectDelete(Fm2uOperationManager* Manager=nullptr)
		:Fm2uOperation(Manager){}

	bool Execute(FString Cmd, FString& Result) override
	{
		const TCHAR* Str = *Cmd;
		bool DidExecute = false;

		if (FParse::Command(&Str, TEXT("DeleteSelected")))
		{
			auto World = GEditor->GetEditorWorldContext().World();
			((UUnrealEdEngine*)GEditor)->edactDeleteSelected(World);

			Result = TEXT("Ok");
			DidExecute = true;
		}

		else if (FParse::Command(&Str, TEXT("DeleteObject")))
		{
			// Deletion of actors in the editor is a dangerous/complex
			// task as actors can be brushes or referenced, levels
			// need to be dirtied and so on there are no "deleteActor"
			// functions in the Editor, only "DeleteSelected".  Since
			// in most cases a deletion is preceded by a selection,
			// and followed by a selection change, we don't bother and
			// just select the object to delete and use the editor
			// function to do it.
			//
			// TODO: maybe we could reselect the previous selection
			//   after the delete op but this is probably in 99% of the
			//   cases not necessary
			GEditor->SelectNone(/*notify=*/false,
								/*deselectBSPSurf=*/true,
		                        /*WarnAboutManyActors=*/false);
			const FString ActorName = FParse::Token(Str,0);
			AActor* Actor = GEditor->SelectNamedActor(*ActorName);
			auto World = GEditor->GetEditorWorldContext().World();
			((UUnrealEdEngine*)GEditor)->edactDeleteSelected(World);

			Result = TEXT("Ok");
			DidExecute = true;
		}

		return DidExecute;
	}
};


class Fm2uOpObjectDuplicate : public Fm2uOperation
{
public:

	Fm2uOpObjectDuplicate(Fm2uOperationManager* Manager=nullptr)
		:Fm2uOperation(Manager){}

	bool Execute(FString Cmd, FString& Result) override
	{
		const TCHAR* Str = *Cmd;
		bool DidExecute = false;

		if (FParse::Command(&Str, TEXT("DuplicateObjects")))
		{
			// UE4 json reader needs an object at top level, so put
			// our list in 'data'.
			FString Json = TEXT("{\"data\": ") + FString(Str) + TEXT("}");
			TSharedPtr<FJsonObject> Object;
			auto Reader = TJsonReaderFactory<TCHAR>::Create(Json);
			if (!FJsonSerializer::Deserialize(Reader, Object)) {
				UE_LOG(LogM2U, Error, TEXT("Error translating Json data."));
				return false;
			}
			if (!Object.IsValid()) {
				UE_LOG(LogM2U, Error, TEXT("Translated Json object not valid."));
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* DupInfoList = nullptr;
			if (!Object->TryGetArrayField(TEXT("data"), DupInfoList)) {
				return false;
			}
			for(auto Entry : *DupInfoList)
			{
				const TSharedPtr<FJsonObject>& EntryObject = Entry->AsObject();
				FString Res = this->_DuplicateObject(EntryObject);
				Result += " " + Res; // TODO: Res can be a space-split string already
				// Needs some actual 'array' to string function or so.
			}

			GEditor->RedrawLevelEditingViewports();

			DidExecute = true;
		}

		return DidExecute;
	}

	FString _DuplicateObject(TSharedPtr<FJsonObject> DupInfo)
	{
		FString Result;
		const FString ActorName = DupInfo->GetStringField(TEXT("original"));
		const FString DuplicateName = DupInfo->GetStringField(TEXT("name"));
		AActor* OrigActor = nullptr;
		AActor* DuplicateActor = nullptr;

		// Find the Original to clone
		if (!m2uHelper::GetActorByName(*ActorName, &OrigActor) ||
		    OrigActor == nullptr)
		{
			UE_LOG(LogM2U, Warning, TEXT("Actor %s not found or invalid."),
			       *ActorName);
			Result = TEXT("NotFound"); // original not found
		}

		// Select only the actor we want to duplicate.
		GEditor->SelectNone(/*notify=*/false, /*deselectBSPSurf=*/true,
		                    /*WarnAboutManyActors=*/false);
		GEditor->SelectActor(OrigActor, /*select=*/true, /*notify=*/false,
		                     /*evenIfHidden=*/true);

		UWorld* World = GEditor->GetEditorWorldContext().World();
		ULevel* CurrentLevel = World->GetCurrentLevel();
		// Do the duplication.
		((UUnrealEdEngine*)GEditor)->edactDuplicateSelected(CurrentLevel,
		                                                    /*bOffsetLocations=*/false);

		// Get the new actor - it will be auto-selected by the editor.
		FSelectionIterator It(GEditor->GetSelectedActorIterator());
		DuplicateActor = static_cast<AActor*>( *It );

		if (! DuplicateActor)
		{
			// Duplication failed.
			Result = TEXT("Failed");
		}

		// If there are transform parameters in the command, apply them.
		m2uHelper::SetActorTransformRelativeFromJson(DuplicateActor, DupInfo);

		// Try to set the actor's name to DuplicateName
		//
		// Note: A unique name was already assigned during the
		//   actual duplicate operation. We could just return that
		//   name instead and say "the editor changed the name" but
		//   if the DuplicateName can be used, it will save a lot
		//   of extra work on the Program side which has to find a
		//   new name otherwise.
		Fm2uOpObjectName Renamer;
		Renamer.RenameActor(DuplicateActor, DuplicateName);

		// Get the editor-created name.
		const FString AssignedName = DuplicateActor->GetFName().ToString();
		if (AssignedName == DuplicateName)
		{
			// If it is the desired name, everything went fine.
			Result = TEXT("Ok");
		}
		else
		{
			// TODO: We can ask the client here for a new name by
			//   directly sending a message back, instead of using it
			//   as a 'result', which would then cause extra renamings
			//   taking place after the next tick().
			// If not, send the name as a response to the caller.
			// Conn->SendResponse(FString::Printf(TEXT("Renamed %s"), *AssignedName));
			Result = FString::Printf(TEXT("Renamed:%s"), *AssignedName);
		}
		return Result;
	}
};


class Fm2uOpObjectAdd : public Fm2uOperation
{
public:

Fm2uOpObjectAdd( Fm2uOperationManager* Manager = NULL )
	:Fm2uOperation( Manager ){}

	bool Execute( FString Cmd, FString& Result ) override
	{
		const TCHAR* Str = *Cmd;
		bool DidExecute = true;

		if (FParse::Command(&Str, TEXT("AddActor")))
		{
			Result = AddActor(Str);
		}

		else if (FParse::Command(&Str, TEXT("AddActorBatch")))
		{
			Result = AddActorBatch(Str);
		}

		else
		{
			// Cannot handle the passed command.
			DidExecute = false;
		}

		return DidExecute;
	}

	/**
	 * Parse a string to interpret what actor to add and what
	 * properties to set.
	 *
	 * Currently only supports name and transform properties.  If the
	 * name is already taken, a replace or edit of the existing object
	 * is possible. If that is not wanted, but the name taken, a new
	 * name will be created.  That name will be returned to the
	 * caller. If the name result is not as desired, the caller might
	 * want to rename the source-object (see object rename functions).
	 */
	FString AddActor(const TCHAR* Str)
	{
		// TODO: Add support for other actors like lights and so on.
		FString AssetName = FParse::Token(Str,0);
		const FString ActorName = FParse::Token(Str,0);
		auto World = GEditor->GetEditorWorldContext().World();
		ULevel* Level = World->GetCurrentLevel();

		// Parse additional parameters
		bool bEditIfExists = true;
		FParse::Bool(Str, TEXT("EditIfExists="), bEditIfExists);
		// Note: Replacing would happen if the object to create is of
		// a different type than the one that already has that desired
		// name.  It is very unlikely that in that case not simply a
		// new name can be used bool bReplaceIfExists = false;

		// FParse::Bool(Str, TEXT("ReplaceIfExists="), bReplaceIfExists);

		// Check if actor with that name already exists if so, modify
		// or replace it (check for additional parameters for that)
		FName ActorFName = m2uHelper::GetFreeName(ActorName);
		AActor* Actor = NULL;
		if ((ActorFName.ToString() != ActorName) && bEditIfExists)
		{
			// name is taken and we want to edit the object that has the name
			if (m2uHelper::GetActorByName( *ActorName, &Actor))
			{
				UE_LOG(LogM2U, Log, TEXT("Found Actor for editing: %s"), *ActorName);
			}
			else {
				UE_LOG(LogM2U, Warning, TEXT("Name already taken, but no Actor with that name found: %s"), *ActorName);
			}
		}
		else
		{
			// name was available or we don't want to edit, so create new actor
			Actor = AddNewActorFromAsset(AssetName, Level, ActorFName, false);
		}

		if (Actor == nullptr)
		{
			//UE_LOG(LogM2U, Log, TEXT("failed creating from asset"));
			return TEXT("1");
		}

		ActorFName = Actor->GetFName();

		// now we might have transformation data in that string
		// so set that, while we already have that actor
		// (no need in searching it again later
		m2uHelper::SetActorTransformRelativeFromText(Actor, Str);
		// TODO: set other attributes
		// TODO: set asset-reference (mesh) at least if bEdit

		// TODO: we might have other property data in that string
		// we need a function to set light radius and all that

		return ActorFName.ToString();
	}

	/**
	 * Add multiple actors from the string.
	 * Every line needs to be a new actor.
	 */
	FString AddActorBatch(const TCHAR* Str)
	{
		UE_LOG(LogM2U, Log, TEXT("Batch Add parsing lines"));
		FString Line;
		while( FParse::Line(&Str, Line, 0) )
		{
			if( Line.IsEmpty() )
				continue;
			UE_LOG(LogM2U, Log, TEXT("Read one Line: %s"),*Line);
			AddActor(*Line);
		}
		// TODO: return a list of the created names
		return TEXT("Ok");
	}

	/**
	 * Spawn a new Actor in the Level. Automatically find the type of
	 * Actor to create based on the type of Asset.
	 *
	 * @param AssetPath The full path to the asset "/Game/Meshes/MyStaticMesh"
	 * @param InLevel The Level to add the Actor to
	 * @param Name The Name to assign to the Actor (should be a valid FName) or NAME_None
	 * @param bSelectActor Select the Actor after it is created
	 *
	 * @return The newly created Actor
	 *
	 * Inspired by the DragDrop functionality of the Viewports, see
	 * LevelEditorViewport::AttemptDropObjAsActors and
	 * SLevelViewport::HandlePlaceDraggedObjects
	 */
	AActor* AddNewActorFromAsset( FString AssetPath,
								  ULevel* InLevel,
								  FName Name = NAME_None,
								  bool bSelectActor = true,
								  EObjectFlags ObjectFlags = RF_Transactional)
	{

		UObject* Asset = m2uAssetHelper::GetAssetFromPath(AssetPath);
		if (Asset == nullptr)
			return nullptr;

		if (Name == NAME_None)
		{
			//Name = FName(TEXT("GeneratedName"));
			Name = FName(*m2uHelper::M2U_GENERATED_NAME);
		}

		const FAssetData AssetData(Asset);
		FText ErrorMessage;
		AActor* Actor = NULL;

		// find the first factory that can create this asset
		for (UActorFactory* ActorFactory : GEditor->ActorFactories)
		{
			if (ActorFactory -> CanCreateActorFrom(AssetData, ErrorMessage))
			{
				Actor = ActorFactory->CreateActor(Asset,
												  InLevel,
												  FTransform::Identity,
												  ObjectFlags,
												  Name);
				if (Actor != nullptr)
				{
					break;
				}
			}
		}

		if (! Actor)
		{
			return nullptr;
		}

		if (bSelectActor)
		{
			GEditor->SelectNone(/*notify=*/false,
								/*deselectBSPSurf=*/true,
		                        /*WarnAboutManyActors=*/false);
			GEditor->SelectActor( Actor, true, true);
		}
		Actor->InvalidateLightingCache();
		Actor->PostEditChange();

		// The Actor will sometimes receive the Name, but not if it is a blueprint?
		// It will never receive the name as Label, so we set the name explicitly
		// again here.
		//Actor->SetActorLabel(Actor->GetFName().ToString());
		// For some reason, since 4.3 the factory will always create a class-based name
		// so we have to rename the actor explicitly completely.
		Fm2uOpObjectName Renamer;
		Renamer.RenameActor(Actor, Name.ToString());

		return Actor;
	} // AActor* AddNewActorFromAsset()
};


class Fm2uOpObjectParent : public Fm2uOperation
{
public:

	Fm2uOpObjectParent( Fm2uOperationManager* Manager = NULL )
		:Fm2uOperation( Manager ){}

	bool Execute( FString Cmd, FString& Result ) override
	{
		const TCHAR* Str = *Cmd;
		bool DidExecute = true;

		if( FParse::Command(&Str, TEXT("ParentChildTo")))
		{
			Result = ParentChildTo(Str);
		}

		else
		{
			// cannot handle the passed command
			DidExecute = false;
		}

		if( DidExecute )
			return true;
		else
			return false;
	}

	FString ParentChildTo(const TCHAR* Str)
	{
		const FString ChildName = FParse::Token(Str,0);
		Str = FCString::Strchr(Str,' ');
		FString ParentName;
		if( Str != NULL) // there may be a parent name present
		{
			Str++;
			if( *Str != '\0' ) // there was a space, but no name after that
			{
				ParentName = FParse::Token(Str,0);
			}
		}

		AActor* ChildActor = NULL;
		if(!m2uHelper::GetActorByName(*ChildName, &ChildActor) || ChildActor == NULL)
		{
			UE_LOG(LogM2U, Log, TEXT("Actor %s not found or invalid."), *ChildName);
			return TEXT("1");
		}

		// TODO: enable transaction?
		//const FScopedTransaction Transaction( NSLOCTEXT("Editor", "UndoAction_PerformAttachment", "Attach actors") );

		// parent to world, aka "detach"
		if( ParentName.Len() < 1) // no valid parent name
		{
			USceneComponent* ChildRoot = ChildActor->GetRootComponent();
			if(ChildRoot->GetAttachParent() != NULL)
			{
				UE_LOG(LogM2U, Log, TEXT("Parenting %s the World."), *ChildName);
				AActor* OldParentActor = ChildRoot->GetAttachParent()->GetOwner();
				OldParentActor->Modify();
				ChildRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
				//ChildActor->SetFolderPath(OldParentActor->GetFolderPath());

				GEngine->BroadcastLevelActorDetached(ChildActor, OldParentActor);
			}
			return TEXT("0");
		}

		AActor* ParentActor = NULL;
		if(!m2uHelper::GetActorByName(*ParentName, &ParentActor) || ParentActor == NULL)
		{
			UE_LOG(LogM2U, Log, TEXT("Actor %s not found or invalid."), *ParentName);
			return TEXT("1");
		}
		if( ParentActor == ChildActor ) // can't parent actor to itself
		{
			return TEXT("1");
		}
		// parent to other actor, aka "attach"
		UE_LOG(LogM2U, Log, TEXT("Parenting %s to %s."), *ChildName, *ParentName);
		GEditor->ParentActors( ParentActor, ChildActor, NAME_None);

		return TEXT("0");
	}

};
