#pragma once

// Identité de l'application : un seul endroit à modifier pour la renommer.
namespace AppInfo {
inline constexpr const char* kName = "Playnest";
inline constexpr const char* kOrganization = "Playnest";
// Anciens noms, du plus récent au plus ancien : leurs données sont reprises au premier lancement.
inline constexpr const char* kOldNames[] = {"Cartouche", "Omnia"};
// Dépôt GitHub où sont publiées les versions de l'application ("owner/repo") : Playnest y cherche ses mises à jour.
inline constexpr const char* kDefaultUpdateRepo = "kgdivin/playnest";
// Fichier de chaque version publiée qui correspond à ce système.
#if defined(_WIN32)
inline constexpr const char* kUpdateAssetPattern = "(?i)(playnest|cartouche).*windows.*\\.zip$";
#elif defined(__APPLE__)
inline constexpr const char* kUpdateAssetPattern = "(?i)playnest.*mac.*\\.(dmg|zip)$";
#else
inline constexpr const char* kUpdateAssetPattern = "(?i)playnest.*\\.appimage$";
#endif
} // namespace AppInfo
