<?php

/**
 * @file
 * Page callbacks for Feeds Import Preview.
 */

/**
 * Form constructor for the feeds import preview form.
 *
 * @see feedspreview_form_validate()
 * @see feedspreview_form_submit()
 */
function feedspreview_form($form, &$form_state, $importer_id) {
  $source = FeedsPreviewSource::instance($importer_id, 0);

  $form = array();
  $form['#importer_id'] = $importer_id;
  $form['#attributes']['enctype'] = 'multipart/form-data';

  $form['help'] = array(
    '#markup' => '<p>' . t('This will give you a brief look of what the parsed result looks like (thus before the data is processed). Note that the data may be modified during processing. If your browser supports it, you can use the left and right arrow keys on your keyboard to navigate between the results.') . '</p>',
  );

  if (isset($form_state['feeds_parser_result'])) {
    $form['feedspreview_result'] = feedspreview_table($source, $form_state['feeds_parser_result']);
  }

  $source_form = $source->configForm($form_state);
  if (!empty($source_form)) {
    $form['feeds'] = array(
      '#type' => 'fieldset',
      '#title' => t('Import settings'),
      '#tree' => TRUE,
      '#collapsed' => isset($form_state['feeds_parser_result']),
      '#collapsible' => isset($form_state['feeds_parser_result']),
    ) + $source_form;
  }

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Preview import'),
  );

  return $form;
}

/**
 * Form validation handler for feedspreview_form().
 *
 * @see feedspreview_form()
 * @see feedspreview_form_submit()
 */
function feedspreview_form_validate($form, &$form_state) {
  FeedsPreviewSource::instance($form['#importer_id'], 0)->configFormValidate($form_state['values']['feeds']);
}

/**
 * Form submission handler for path_admin_form().
 *
 * @see feedspreview_form()
 * @see feedspreview_form_validate()
 */
function feedspreview_form_submit($form, &$form_state) {
  // Save source and import.
  $source = FeedsPreviewSource::instance($form['#importer_id'], 0);

  if (!empty($form_state['values']['feeds']) && is_array($form_state['values']['feeds'])) {
    $source->addConfig($form_state['values']['feeds']);
    $source->save();
  }

  $form_state['feeds_parser_result'] = $source->preview();
  $form_state['rebuild'] = TRUE;
}

/**
 * Builds a preview table.
 *
 * @param FeedsSource $feeds_source
 *   The import source.
 * @param FeedsParserResult $result
 *   The parser result.
 *
 * @return array
 *   A build array in the format expected by drupal_render().
 */
function feedspreview_table(FeedsSource $feeds_source, FeedsParserResult $result) {
  // Check the result's source.
  if (empty($result->items)) {
    drupal_set_message(t('No result.'));
    return;
  }

  $table_builder = new FeedsPreviewTable($feeds_source);
  return $table_builder->build($result);
}
