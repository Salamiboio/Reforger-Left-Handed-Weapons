[ComponentEditorProps(category: "SAL/Weapons", description: "Attach RightHandProp to the left hand using an offset captured by the attach animation event")]
class SAL_LeftHandedGunClass: ScriptComponentClass
{
}

class SAL_LeftHandedGun: ScriptComponent
{
	[Attribute("1.0", UIWidgets.EditBox, "Angular threshold for diagnostic logging, in degrees", "0 180 0.1")]
	protected float m_fAngularTolerance;

	[Attribute("0.001", UIWidgets.EditBox, "Position threshold for diagnostic logging, in metres", "0 0.05 0.001")]
	protected float m_fPositionTolerance;

	[Attribute("0", UIWidgets.CheckBox, "Debug: log each frame, grip captures, corrections and rack markers")]
	protected bool m_bDebug;

	protected SCR_ChimeraCharacter m_Character;
	protected TNodeId m_LeftHandBone = -1;
	protected TNodeId m_RightHandPropBone = -1;
	protected vector m_GripOffset[4];
	protected bool m_bHasGripOffset;
	protected bool m_bCaptureOnAttach;
	protected int m_iPreparedFrame = -1;
	protected bool m_bHasVisualModifier;
	protected bool m_bDetachedByAnimation = true;
	protected AnimationEventID m_AttachGunEvent = -1;
	protected AnimationEventID m_DetachGunEvent = -1;

	protected bool m_HasPrevious;
	protected vector m_PreviousProp;
	protected vector m_PreviousGun;
	protected int m_LastFrame = -1;
	protected int m_RackSequence;
	protected float m_RackStartMs = -1;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		m_AttachGunEvent = GameAnimationUtils.RegisterAnimationEvent("SAL_AttachGunToLeftHand");
		m_DetachGunEvent = GameAnimationUtils.RegisterAnimationEvent("SAL_DetachGunFromLeftHand");
		SetEventMask(owner, EntityEvent.FRAME | EntityEvent.POSTFRAME);
	}

	override void OnDelete(IEntity owner)
	{
		ResetVisualModifier();
		super.OnDelete(owner);
	}

	void HandleGunAttachmentEvent(AnimationEventID eventId)
	{
		if (eventId != m_AttachGunEvent && eventId != m_DetachGunEvent)
			return;

		m_bDetachedByAnimation = eventId == m_DetachGunEvent;
		m_bHasGripOffset = false;
		m_bCaptureOnAttach = !m_bDetachedByAnimation;
		// Capture the evaluated animation pose once in PostFrame after each attach.
		ResetVisualModifier();

		if (m_bDebug)
			PrintFormat("[SAL LeftHandedGun EVENT] client=%1 weapon=%2 detached=%3 event=%4",
				SCR_PlayerController.GetLocalPlayerId(),
				Replication.FindItemId(GetOwner().FindComponent(WeaponComponent)),
				m_bDetachedByAnimation, GameAnimationUtils.GetEventString(eventId));
	}

	void RecaptureReference()
	{
		ResetVisualModifier();
		m_bHasGripOffset = false;
		m_iPreparedFrame = -1;
		m_HasPrevious = false;
		m_LastFrame = -1;
	}

	override void EOnFrame(IEntity owner, float timeSlice)
	{
		// Remove the previous visual modifier before this frame's animation
		// evaluation. Attachment always uses a fresh base pose.
		m_iPreparedFrame = -1;
		if (!ResetVisualModifier())
			return;

		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(owner.GetRootParent());
		if (!character)
		{
			m_HasPrevious = false;
			m_Character = null;
			return;
		}

		BaseWeaponManagerComponent manager = character.GetWeaponManager();
		BaseWeaponComponent weapon;
		if (manager)
			weapon = manager.GetCurrentWeapon();

		if (!weapon || weapon.GetOwner() != owner)
		{
			m_HasPrevious = false;
			return;
		}

		Animation animation = character.GetAnimation();
		if (!animation)
			return;

		if (character != m_Character)
		{
			m_Character = character;
			RecaptureReference();
			m_LeftHandBone = animation.GetBoneIndex("LeftHand");
			m_RightHandPropBone = animation.GetBoneIndex("RightHandProp");
			if (m_bDebug && (m_LeftHandBone == -1 || m_RightHandPropBone == -1))
				Print("[SAL LeftHandedGun] Missing LeftHand or RightHandProp on the holder");
		}

		if (m_LeftHandBone == -1 || m_RightHandPropBone == -1)
			return;

		m_iPreparedFrame = owner.GetWorld().GetFrameNumber();
	}

	override void EOnPostFrame(IEntity owner, float timeSlice)
	{
		if (!m_Character || m_iPreparedFrame != owner.GetWorld().GetFrameNumber())
			return;

		if (m_bDebug)
			LogFrame(owner, timeSlice);

		if (m_bDetachedByAnimation)
			return;

		Animation animation = m_Character.GetAnimation();
		if (!animation)
			return;

		vector leftTM[4];
		vector propTM[4];
		if (!animation.GetBoneMatrix(m_LeftHandBone, leftTM)
			|| !animation.GetBoneMatrix(m_RightHandPropBone, propTM))
			return;

		vector leftRotation[3];
		vector propRotation[3];
		for (int axis = 0; axis < 3; axis++)
		{
			if (leftTM[axis].LengthSq() < 0.000001 || propTM[axis].LengthSq() < 0.000001)
				return;

			leftRotation[axis] = leftTM[axis].Normalized();
			propRotation[axis] = propTM[axis].Normalized();
		}

		if (m_bCaptureOnAttach)
		{
			for (int sampleAxis = 0; sampleAxis < 3; sampleAxis++)
				m_GripOffset[sampleAxis] = propRotation[sampleAxis].InvMultiply3(leftRotation);
			m_GripOffset[3] = (propTM[3] - leftTM[3]).InvMultiply3(leftRotation);
			m_bHasGripOffset = true;
			m_bCaptureOnAttach = false;
			if (m_bDebug)
				PrintFormat("[SAL LeftHandedGun] captured grip client=%1 frame=%2 position=%3 forward=%4 up=%5",
					SCR_PlayerController.GetLocalPlayerId(), owner.GetWorld().GetFrameNumber(),
					m_GripOffset[3], m_GripOffset[2], m_GripOffset[1]);
		}

		if (!m_bHasGripOffset)
			return;

		vector targetRotation[3];
		for (int targetAxis = 0; targetAxis < 3; targetAxis++)
			targetRotation[targetAxis] = m_GripOffset[targetAxis].Multiply3(leftRotation);
		vector targetPosition = m_GripOffset[3].Multiply3(leftRotation) + leftTM[3];

		// Convert the desired model-space pose to a fresh bone-local modifier.
		// Each frame replaces the modifier; previous corrections are not added.
		vector modifierTM[4];
		for (int modifierAxis = 0; modifierAxis < 3; modifierAxis++)
			modifierTM[modifierAxis] = targetRotation[modifierAxis].InvMultiply3(propRotation);
		modifierTM[3] = (targetPosition - propTM[3]).InvMultiply3(propRotation);

		bool accepted = animation.SetBoneMatrix(m_Character, m_RightHandPropBone, modifierTM);
		m_bHasVisualModifier = accepted;

		if (!m_bDebug)
			return;

		float degrees = AngularDifference(propRotation[0], propRotation[1], propRotation[2], targetRotation[0], targetRotation[1], targetRotation[2]);
		float distance = vector.Distance(propTM[3], targetPosition);
		if (accepted && degrees <= Math.Max(0, m_fAngularTolerance) && distance <= Math.Max(0, m_fPositionTolerance))
			return;

		string view = "OBSERVER";
		if (SCR_PlayerController.GetLocalControlledEntity() == m_Character)
			view = "HOLDER";

		PrintFormat("[SAL LeftHandedGun] attach client=%1 view=%2 frame=%3 degrees=%4 positionCm=%5 accepted=%6",
			SCR_PlayerController.GetLocalPlayerId(), view, owner.GetWorld().GetFrameNumber(),
			degrees, distance * 100, accepted);
	}

	void RecordRackCommand(int commandID, int value)
	{
		if (!m_bDebug || (value != 1337 && value != 1338))
			return;

		m_RackSequence++;
		m_RackStartMs = GetOwner().GetWorld().GetWorldTime();
		WeaponComponent weapon = WeaponComponent.Cast(GetOwner().FindComponent(WeaponComponent));
		RplId weaponId = Replication.FindItemId(weapon);
		PrintFormat("[SAL LeftHandedGun EVENT] client=%1 weapon=%2 rack=%3 command=%4 value=%5 ms=%6",
			SCR_PlayerController.GetLocalPlayerId(), weaponId, m_RackSequence, commandID, value, m_RackStartMs);
	}

	// Sample the animation pose before this frame's visual modifier is submitted.
	// Same-callback readback does not confirm that the engine applied a modifier.
	protected void LogFrame(IEntity owner, float timeSlice)
	{
		if (!m_bDebug)
			return;

		Animation animation = m_Character.GetAnimation();
		BaseWeaponManagerComponent manager = m_Character.GetWeaponManager();
		if (!animation || !manager)
			return;
		BaseWeaponComponent weapon = manager.GetCurrentWeapon();
		if (!weapon || weapon.GetOwner() != owner)
			return;
		int frame = owner.GetWorld().GetFrameNumber();
		if (frame == m_LastFrame)
			return;
		vector propTM[4];
		if (!animation.GetBoneMatrix(m_RightHandPropBone, propTM))
			return;

		m_LastFrame = frame;
		vector characterTM[4];
		m_Character.GetWorldTransform(characterTM);
		vector model = propTM[3];
		vector world = model.Multiply4(characterTM);
		vector gunModel = owner.GetOrigin().InvMultiply4(characterTM);
		vector deltaModel = "0 0 0";
		vector deltaGun = "0 0 0";
		if (m_HasPrevious)
		{
			deltaModel = model - m_PreviousProp;
			deltaGun = gunModel - m_PreviousGun;
		}
		m_PreviousProp = model;
		m_PreviousGun = gunModel;
		m_HasPrevious = true;

		RplId weaponId = Replication.FindItemId(weapon);
		RplNode node = Replication.FindNode(weaponId);
		string replication = "unreplicated";
		if (node)
			replication = string.Format("owner=%1 role=%2", node.IsOwner(), node.GetRole());

		IEntity localCharacter = SCR_PlayerController.GetLocalControlledEntity();
		string view = "OBSERVER";
		if (!localCharacter)
			view = "NO_LOCAL_PLAYER";
		else if (localCharacter == m_Character)
			view = "HOLDER";

		CharacterControllerComponent controller = m_Character.GetCharacterController();
		if (!controller)
			return;

		float now = owner.GetWorld().GetWorldTime();
		float rackMs = -1;
		if (m_RackStartMs >= 0)
			rackMs = now - m_RackStartMs;

		string identity = string.Format("client=%1 view=%2 weapon=%3 %4",
			SCR_PlayerController.GetLocalPlayerId(), view, weaponId, replication);
		string state = string.Format("rack=%1 rackMs=%2 ADS=%3 reloading=%4",
			m_RackSequence, rackMs, controller.IsWeaponADS(), controller.IsReloading());
		PrintFormat("[SAL LeftHandedGun FRAME] %1 %2 frame=%3 ms=%4 dt=%5 baseModel=%6 world=%7 deltaModel=%8 forward=%9",
			identity, state, frame, now, timeSlice, model, world, deltaModel, propTM[2]);
		PrintFormat("[SAL LeftHandedGun GUN] %1 rack=%2 frame=%3 model=%4 deltaModel=%5 propOffset=%6",
			identity, m_RackSequence, frame, gunModel, deltaGun, gunModel - model);
	}

	protected bool ResetVisualModifier()
	{
		if (!m_bHasVisualModifier)
			return true;

		if (m_Character && m_RightHandPropBone != -1)
		{
			Animation animation = m_Character.GetAnimation();
			if (!animation)
				return false;

			vector identity[4];
			Math3D.MatrixIdentity4(identity);
			if (!animation.SetBoneMatrix(m_Character, m_RightHandPropBone, identity))
				return false;
		}

		m_bHasVisualModifier = false;
		return true;
	}

	// Explicit axes avoid passing fixed-size matrices through unsized array parameters.
	protected float AngularDifference(vector firstX, vector firstY, vector firstZ, vector secondX, vector secondY, vector secondZ)
	{
		float trace = vector.Dot(firstX, secondX) + vector.Dot(firstY, secondY) + vector.Dot(firstZ, secondZ);
		return Math.Acos(Math.Clamp((trace - 1) * 0.5, -1, 1)) * 57.2957795;
	}
}

// Player animation events run on each client's character representation.
// Route to the equipped weapon without requiring the local player to hold it.
modded class SCR_CharacterControllerComponent
{
	override void OnAnimationEvent(AnimationEventID animEventType, AnimationEventID animUserString, int intParam, float timeFromStart, float timeToEnd)
	{
		super.OnAnimationEvent(animEventType, animUserString, intParam, timeFromStart, timeToEnd);

		string eventName = GameAnimationUtils.GetEventString(animEventType);
		if (eventName != "SAL_AttachGunToLeftHand" && eventName != "SAL_DetachGunFromLeftHand")
			return;

		BaseWeaponManagerComponent manager = GetWeaponManagerComponent();
		if (!manager)
			return;
		BaseWeaponComponent weapon = manager.GetCurrentWeapon();
		if (!weapon)
			return;

		SAL_LeftHandedGun attachment = SAL_LeftHandedGun.Cast(weapon.GetOwner().FindComponent(SAL_LeftHandedGun));
		if (attachment)
			attachment.HandleGunAttachmentEvent(animEventType);
	}
}
