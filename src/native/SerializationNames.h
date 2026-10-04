// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once

#include <array>
#include <string_view>

namespace Clipboard::SerializationNames
{
	// Canonical names are written by every current functor. Legacy names are
	// lookup-only aliases; their factories create those same canonical objects.
	inline constexpr char kCreateSelectionBoxName[] = "Clipboard.CreateSelectionBoxFunctor";
	inline constexpr char kCreateSelectionBoxLegacyName[] = "ClipboardResurrection.CreateSelectionBoxFunctor";
	inline constexpr char kScrapSelectionName[] = "Clipboard.ScrapSelectionFunctor";
	inline constexpr char kScrapSelectionLegacyName[] = "ClipboardResurrection.ScrapSelectionFunctor";
	inline constexpr char kScrapObjectsName[] = "Clipboard.ScrapObjectsFunctor";
	inline constexpr char kScrapObjectsLegacyName[] = "ClipboardResurrection.ScrapObjectsFunctor";
	inline constexpr char kSendWorkshopEventName[] = "Clipboard.SendWorkshopEventToSelectedObjectsFunctor";
	inline constexpr char kSendWorkshopEventLegacyName[] = "ClipboardResurrection.SendWorkshopEventToSelectedObjectsFunctor";
	inline constexpr char kEnableObjectsName[] = "Clipboard.EnableObjectsFunctor";
	inline constexpr char kEnableObjectsLegacyName[] = "ClipboardResurrection.EnableObjectsFunctor";
	inline constexpr char kDisableObjectsName[] = "Clipboard.DisableObjectsFunctor";
	inline constexpr char kDisableObjectsLegacyName[] = "ClipboardResurrection.DisableObjectsFunctor";
	inline constexpr char kScaleSelectionName[] = "Clipboard.ScaleSelectionFunctor";
	inline constexpr char kScaleSelectionLegacyName[] = "ClipboardResurrection.ScaleSelectionFunctor";
	inline constexpr char kTransmitPowerName[] = "Clipboard.TransmitPowerInSelectionFunctor";
	inline constexpr char kTransmitPowerLegacyName[] = "ClipboardResurrection.TransmitPowerInSelectionFunctor";
	inline constexpr char kPastePatternObjectsName[] = "Clipboard.PastePatternObjectsFunctor";
	inline constexpr char kPastePatternObjectsLegacyName[] = "ClipboardResurrection.PastePatternObjectsFunctor";
	inline constexpr char kPastePatternWiresName[] = "Clipboard.PastePatternWiresFunctor";
	inline constexpr char kPastePatternWiresLegacyName[] = "ClipboardResurrection.PastePatternWiresFunctor";
	inline constexpr char kPastePatternWiresForRowsName[] = "Clipboard.PastePatternWiresForRowsFunctor";
	inline constexpr char kPastePatternWiresForRowsLegacyName[] = "ClipboardResurrection.PastePatternWiresForRowsFunctor";
	inline constexpr char kPrepareImportedObjectsName[] = "Clipboard.PrepareImportedObjectsFunctor";
	inline constexpr char kPrepareImportedObjectsLegacyName[] = "ClipboardResurrection.PrepareImportedObjectsFunctor";
	inline constexpr char kGetImportedPowerGeneratorsName[] = "Clipboard.GetImportedPowerGeneratorsFunctor";
	inline constexpr char kGetImportedPowerGeneratorsLegacyName[] = "ClipboardResurrection.GetImportedPowerGeneratorsFunctor";
	inline constexpr char kReconnectImportedPowerName[] = "Clipboard.ReconnectImportedPowerFunctor";
	inline constexpr char kReconnectImportedPowerLegacyName[] = "ClipboardResurrection.ReconnectImportedPowerFunctor";
	inline constexpr char kInitializeImportedWorkshopObjectsName[] = "Clipboard.InitializeImportedWorkshopObjectsFunctor";
	inline constexpr char kInitializeImportedWorkshopObjectsLegacyName[] = "ClipboardResurrection.InitializeImportedWorkshopObjectsFunctor";
	// New operations have never written legacy names.
	inline constexpr char kTryScaleSelectionName[] = "Clipboard.TryScaleSelectionFunctor";
	inline constexpr char kPrepareImportedRowsName[] = "Clipboard.PrepareImportedRowsFunctor";
	inline constexpr char kInitializeImportedWorkshopRowsName[] = "Clipboard.InitializeImportedWorkshopRowsFunctor";
	inline constexpr char kReconnectImportedPowerForRowsName[] = "Clipboard.ReconnectImportedPowerForRowsFunctor";
	inline constexpr char kConnectImportedPowerForRowsName[] = "Clipboard.ConnectImportedPowerForRowsFunctor";
	inline constexpr char kRefreshImportedPowerForRowsName[] = "Clipboard.RefreshImportedPowerForRowsFunctor";
	inline constexpr char kPastePatternObjectsWithReuseName[] = "Clipboard.PastePatternObjectsWithReuseFunctor";
	inline constexpr char kScrapObjectsPacedName[] = "Clipboard.ScrapObjectsPacedFunctor";
	inline constexpr char kGetImportedAnimationCandidatesName[] = "Clipboard.GetImportedAnimationCandidatesFunctor";

	struct Names
	{
		std::string_view canonical;
		std::string_view legacy;
	};
	inline constexpr std::array kNames{
		Names{ kCreateSelectionBoxName, kCreateSelectionBoxLegacyName },
		Names{ kScrapSelectionName, kScrapSelectionLegacyName },
		Names{ kScrapObjectsName, kScrapObjectsLegacyName },
		Names{ kSendWorkshopEventName, kSendWorkshopEventLegacyName },
		Names{ kEnableObjectsName, kEnableObjectsLegacyName },
		Names{ kDisableObjectsName, kDisableObjectsLegacyName },
		Names{ kScaleSelectionName, kScaleSelectionLegacyName },
		Names{ kTransmitPowerName, kTransmitPowerLegacyName },
		Names{ kPastePatternObjectsName, kPastePatternObjectsLegacyName },
		Names{ kPastePatternWiresName, kPastePatternWiresLegacyName },
		Names{ kPastePatternWiresForRowsName, kPastePatternWiresForRowsLegacyName },
		Names{ kPrepareImportedObjectsName, kPrepareImportedObjectsLegacyName },
		Names{ kGetImportedPowerGeneratorsName, kGetImportedPowerGeneratorsLegacyName },
		Names{ kReconnectImportedPowerName, kReconnectImportedPowerLegacyName },
		Names{ kInitializeImportedWorkshopObjectsName, kInitializeImportedWorkshopObjectsLegacyName }
	};
	consteval bool ValidNames()
	{
		for (std::size_t i = 0; i < kNames.size(); ++i) {
			if (!kNames[i].canonical.starts_with("Clipboard.") ||
				!kNames[i].legacy.starts_with("ClipboardResurrection.") ||
				kNames[i].canonical.substr(10) != kNames[i].legacy.substr(22)) {
				return false;
			}
			for (std::size_t j = i + 1; j < kNames.size(); ++j) {
				if (kNames[i].canonical == kNames[j].canonical || kNames[i].legacy == kNames[j].legacy) {
					return false;
				}
			}
		}
		return true;
	}
	static_assert(kNames.size() == 15);
	static_assert(ValidNames());
}
