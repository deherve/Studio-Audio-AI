# Langues de Studio YuE2 · Languages · Idiomas

Un fichier JSON par langue. Chaque fichier se traduit seul et se partage seul.
One JSON file per language: each one can be translated and shared on its own.

| Fichier | Langue |
|---|---|
| `fr.json` | Français (textes d'origine) |
| `en.json` | English |
| `es.json` | Español |
| `it.json` | Italiano |
| `de.json` | Deutsch |
| `pt.json` | Português |
| `ja.json` | 日本語 |
| `ko.json` | 한국어 |
| `zh-Hans.json` | 简体中文（普通话） |
| `yue.json` | 粵語（廣東話） |
| `_modele.json` | Gabarit vide / empty template (n'apparaît pas dans la liste) |

## Corriger une traduction · Fix a translation

1. Ouvrez le fichier de la langue (dans un éditeur de texte UTF-8).
2. Dans `"strings"`, modifiez la **valeur** (à droite des deux-points). Ne touchez pas à la **clé** (à gauche, le texte français d'origine).
3. Relancez Studio YuE2 (ou rechangez de langue dans Réglages) : la correction est prise en compte.

## Ajouter une langue · Add a language

1. Copiez `_modele.json` en `<code>.json` (par ex. `nl.json`, `ru.json`, `pl.json`).
2. Dans `"meta"`, renseignez `"name"` (nom de la langue dans cette langue) et `"script"` :
   `latin` (par défaut, couvre aussi le grec et le cyrillique), `ja`, `ko`, `zh-Hans` ou `zh-Hant`.
3. Remplissez les valeurs de `"strings"`. Une valeur vide retombe sur l'anglais, puis sur le français.
4. Relancez l'application : la langue apparaît dans Réglages › Langue de l'application.

## Règles de traduction · Rules

- Gardez **les mêmes marques de format** que le texte français : `%d`, `%s`, `%zu`, `%.1f`, `%02d`… Le nombre et le type doivent rester identiques. Si l'ordre des mots l'exige, vous pouvez numéroter les marques : `%2$s` puis `%1$d` (uniquement dans les textes qui contiennent plusieurs marques).
- Gardez les sauts de ligne `\n` des textes qui en contiennent.
- Un `%%` affiche un signe `%`.
- Les balises de style (`rock`, `male vocal`…) restent en anglais : ce sont les mots que comprend le modèle.
- Les textes très longs s'affichent sur plusieurs lignes ; les boutons et libellés courts doivent rester concis (la fenêtre a une largeur minimale).

## Pour les développeurs · For developers

Les textes de l'interface sont écrits en français dans le code, entre `tr("...")`, `trf("...")`, `wtr("...")` ou `N_("...")`. Après toute modification de texte :

```
python native/tools/i18n_sync.py
```

ajoute les nouveaux textes à tous les fichiers (valeur vide), retire ceux qui n'existent plus et vérifie les marques de format (`--check` pour vérifier sans écrire). Ajoutez ensuite les traductions manquantes dans chaque fichier.
